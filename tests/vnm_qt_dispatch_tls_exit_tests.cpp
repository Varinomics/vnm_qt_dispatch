#include <vnm_qt_dispatch/vnm_qt_dispatch.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QProcess>
#include <QString>

#include <atomic>
#include <cstdio>
#include <exception>
#include <string>
#include <thread>

namespace {

constexpr int k_child_timeout_ms           = 30000;
constexpr int k_dispatch_iterations        = 256;
constexpr int k_dispatches_per_iteration   = 5;

class Dispatch_target : public QObject
{
public:
    explicit Dispatch_target(std::thread::id owner_thread)
        :
            m_owner_thread(owner_thread)
    {}

    void record() noexcept
    {
        if (std::this_thread::get_id() != m_owner_thread) {
            m_wrong_thread.store(true, std::memory_order_release);
        }
        m_execution_count.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] int execution_count() const noexcept
    {
        return m_execution_count.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool used_wrong_thread() const noexcept
    {
        return m_wrong_thread.load(std::memory_order_acquire);
    }

private:
    std::thread::id m_owner_thread;
    std::atomic_int m_execution_count = 0;
    std::atomic_bool m_wrong_thread   = false;
};

struct Worker_result
{
    bool succeeded = false;
    std::string error;
};

bool require(bool condition, const char* message, Worker_result* result)
{
    if (condition) {
        return true;
    }

    result->error = message;
    return false;
}
int run_child(QCoreApplication* application)
{
    Dispatch_target target(std::this_thread::get_id());
    Worker_result worker_result;

    std::thread worker(
        [&]()
        {
            try {
                for (int iteration = 0; iteration < k_dispatch_iterations; ++iteration) {
                    const int call_result = vnm::qt::blocking_call(
                        &target,
                        [&target, iteration]()
                        {
                            target.record();
                            return iteration;
                        });
                    if (!require(
                            call_result == iteration,
                            "blocking_call() returned the wrong value.",
                            &worker_result)) {
                        break;
                    }

                    const auto member_post_result =
                        vnm::qt::post_with_exception_reporter(
                            &target,
                            &Dispatch_target::record,
                            nullptr);
                    if (!require(
                            member_post_result == vnm::qt::Post_result::QUEUED,
                            "The canonical member post was not accepted.",
                            &worker_result)) {
                        break;
                    }

                    vnm::qt::blocking_call(
                        &target,
                        &Dispatch_target::record);

                    const auto task_post_result =
                        vnm::qt::post_with_exception_reporter(
                            &target,
                            [&target]()
                            {
                                target.record();
                            },
                            nullptr);
                    if (!require(
                            task_post_result == vnm::qt::Post_result::QUEUED,
                            "The canonical task post was not accepted.",
                            &worker_result)) {
                        break;
                    }

                    vnm::qt::blocking_call(
                        &target,
                        [&target]()
                        {
                            target.record();
                        });
                }

                if (worker_result.error.empty()) {
                    const int observed_count = vnm::qt::blocking_call(
                        &target,
                        [&target]()
                        {
                            return target.execution_count();
                        });
                    const int expected_count =
                        k_dispatch_iterations * k_dispatches_per_iteration;

                    if (require(
                            observed_count == expected_count,
                            "Not all accepted dispatches executed.",
                            &worker_result) &&
                        require(
                            !target.used_wrong_thread(),
                            "A dispatch executed outside the receiver thread.",
                            &worker_result)) {
                        worker_result.succeeded = true;
                    }
                }
            }
            catch (const std::exception& error) {
                worker_result.error =
                    std::string("Unexpected dispatch exception: ") + error.what();
            }
            catch (...) {
                worker_result.error = "Unexpected non-standard dispatch exception.";
            }

            QMetaObject::invokeMethod(
                application,
                [application]()
                {
                    application->quit();
                },
                Qt::QueuedConnection);
        });

    application->exec();
    worker.join();

    if (!worker_result.succeeded) {
        std::fprintf(
            stderr,
            "FAIL: foreign dispatch worker: %s\n",
            worker_result.error.empty()
                ? "unknown failure"
                : worker_result.error.c_str());
        return 1;
    }

    return 0;
}

int run_child_process(
    const QCoreApplication& application,
    const QString& argument,
    const char* description)
{
    QProcess child;
    child.setProgram(application.applicationFilePath());
    child.setArguments({argument});
    child.start();

    if (!child.waitForStarted(k_child_timeout_ms)) {
        std::fprintf(
            stderr,
            "FAIL: %s child process did not start.\n",
            description);
        return 1;
    }
    if (!child.waitForFinished(k_child_timeout_ms)) {
        child.kill();
        child.waitForFinished();
        std::fprintf(
            stderr,
            "FAIL: %s child process timed out.\n",
            description);
        return 1;
    }

    const QByteArray child_stderr = child.readAllStandardError();
    if (child.exitStatus() != QProcess::NormalExit) {
        std::fprintf(
            stderr,
            "FAIL: %s child terminated abnormally (exit code %d).\n%s",
            description,
            child.exitCode(),
            child_stderr.constData());
        return 1;
    }
    if (child.exitCode() != 0) {
        std::fprintf(
            stderr,
            "FAIL: %s child returned exit code %d.\n%s",
            description,
            child.exitCode(),
            child_stderr.constData());
        return 1;
    }

    return 0;
}

int run_parent(const QCoreApplication& application)
{
    return run_child_process(
        application,
        QStringLiteral("--dispatch-child"),
        "dispatch");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    if (application.arguments().contains("--dispatch-child")) {
        return run_child(&application);
    }
    return run_parent(application);
}
