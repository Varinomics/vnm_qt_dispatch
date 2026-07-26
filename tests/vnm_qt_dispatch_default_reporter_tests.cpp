#include <vnm_qt_dispatch/vnm_qt_dispatch.h>

#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

class Dispatch_target : public QObject
{
    Q_OBJECT

public:
    int throwing_member_calls() const
    {
        return m_throwing_member_calls.load(std::memory_order_acquire);
    }

public Q_SLOTS:
    void throw_target_exception()
    {
        m_throwing_member_calls.fetch_add(1, std::memory_order_release);
        throw std::runtime_error("member post failure");
    }

private:
    std::atomic<int> m_throwing_member_calls{0};
};

template <typename Receiver>
class Worker_receiver_fixture
{
public:
    Worker_receiver_fixture()
    :
        m_receiver(new Receiver)
    {
        m_receiver->moveToThread(&m_thread);
        QObject::connect(
            &m_thread,
            &QThread::finished,
            m_receiver,
            &QObject::deleteLater);
        m_thread.start();
    }

    ~Worker_receiver_fixture()
    {
        m_thread.quit();
        m_thread.wait();
    }

    Worker_receiver_fixture(const Worker_receiver_fixture&) = delete;
    Worker_receiver_fixture& operator=(const Worker_receiver_fixture&) = delete;

    Receiver* receiver() const
    {
        return m_receiver;
    }

private:
    QThread m_thread;
    Receiver* m_receiver = nullptr;
};

class Scoped_message_handler
{
public:
    Scoped_message_handler()
    {
        QMutexLocker lock(&s_mutex);
        s_messages.clear();
        m_previous_handler = qInstallMessageHandler(handle_message);
    }

    ~Scoped_message_handler()
    {
        qInstallMessageHandler(m_previous_handler);
    }

    bool contains(const QString& needle) const
    {
        QMutexLocker lock(&s_mutex);
        for (const QString& message : std::as_const(s_messages)) {
            if (message.contains(needle)) {
                return true;
            }
        }
        return false;
    }

private:
    static void handle_message(
        QtMsgType,
        const QMessageLogContext&,
        const QString& message)
    {
        QMutexLocker lock(&s_mutex);
        s_messages.push_back(message);
    }

    static inline QMutex s_mutex;
    static inline std::vector<QString> s_messages;
    QtMessageHandler m_previous_handler = nullptr;
};

} // namespace

class Vnm_qt_dispatch_default_reporter_tests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void post_target_exceptions_are_reported_and_event_loop_survives();
};

void Vnm_qt_dispatch_default_reporter_tests::
post_target_exceptions_are_reported_and_event_loop_survives()
{
    auto callable_calls = std::make_shared<std::atomic<int>>(0);
    auto queued_successes = std::make_shared<std::atomic<int>>(0);

    Scoped_message_handler captured_messages;
    Worker_receiver_fixture<Dispatch_target> worker;

    const auto callable_result = vnm::qt::post(
        worker.receiver(),
        [callable_calls]() {
            callable_calls->fetch_add(1, std::memory_order_release);
            throw std::runtime_error("callable post failure");
        });
    const auto member_result = vnm::qt::post(
        worker.receiver(),
        &Dispatch_target::throw_target_exception);

    QCOMPARE(callable_result, vnm::qt::Post_result::QUEUED);
    QCOMPARE(member_result, vnm::qt::Post_result::QUEUED);

    const int call_result = vnm::qt::call(
        worker.receiver(),
        []() {
            return 31;
        });
    QCOMPARE(call_result, 31);
    QCOMPARE(callable_calls->load(std::memory_order_acquire), 1);
    QCOMPARE(worker.receiver()->throwing_member_calls(), 1);

    const auto successful_post_result = vnm::qt::post(
        worker.receiver(),
        [queued_successes]() {
            queued_successes->fetch_add(1, std::memory_order_release);
        });

    QCOMPARE(successful_post_result, vnm::qt::Post_result::QUEUED);
    QTRY_COMPARE_WITH_TIMEOUT(
        queued_successes->load(std::memory_order_acquire),
        1,
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        captured_messages.contains(
            QStringLiteral("Queued target threw an exception")),
        2000);
    QVERIFY(captured_messages.contains(QStringLiteral("callable post failure")));
    QVERIFY(captured_messages.contains(QStringLiteral("member post failure")));
}

QTEST_GUILESS_MAIN(Vnm_qt_dispatch_default_reporter_tests)

#include "vnm_qt_dispatch_default_reporter_tests.moc"
