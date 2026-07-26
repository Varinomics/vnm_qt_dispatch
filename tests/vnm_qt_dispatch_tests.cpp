#include <vnm_qt_dispatch/vnm_qt_dispatch.h>

#include <QtTest/QTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QSemaphore>
#include <QThread>

#include <atomic>
#include <concepts>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

constexpr int ASYNC_TIMEOUT_MS = 3000;
std::atomic_bool capture_failure_enabled = true;

class Target_exception : public std::runtime_error
{
public:
    Target_exception()
        :
            std::runtime_error("target exception")
    {
    }
};

struct Exact_void_lvalue_task
{
    void operator()() &
    {
    }
};

struct Nonvoid_lvalue_task
{
    int operator()() &
    {
        return 1;
    }
};

struct Rvalue_only_void_task
{
    void operator()() &&
    {
    }
};

struct Move_only_lvalue_task
{
    Move_only_lvalue_task() = default;
    Move_only_lvalue_task(const Move_only_lvalue_task&) = delete;
    Move_only_lvalue_task& operator=(const Move_only_lvalue_task&) = delete;
    Move_only_lvalue_task(Move_only_lvalue_task&&) = default;
    Move_only_lvalue_task& operator=(Move_only_lvalue_task&&) = default;

    void operator()() &
    {
    }
};

struct Explicit_move_lvalue_task
{
    Explicit_move_lvalue_task() = default;
    Explicit_move_lvalue_task(const Explicit_move_lvalue_task&) = delete;
    explicit Explicit_move_lvalue_task(Explicit_move_lvalue_task&&)
    {}

    void operator()() &
    {
    }
};

struct Explicit_move_reporter
{
    Explicit_move_reporter() = default;
    Explicit_move_reporter(const Explicit_move_reporter&) = delete;
    explicit Explicit_move_reporter(Explicit_move_reporter&&)
    {}

    void operator()(std::exception_ptr) &
    {
    }
};

struct Throwing_move_reporter
{
    Throwing_move_reporter() = default;
    Throwing_move_reporter(const Throwing_move_reporter&) = delete;

    Throwing_move_reporter(Throwing_move_reporter&&)
    {
        if (capture_failure_enabled.load(std::memory_order_relaxed)) {
            throw std::runtime_error("reporter capture failed");
        }
    }

    void operator()(std::exception_ptr) &
    {
    }
};

struct Throwing_destructor_reporter
{
    ~Throwing_destructor_reporter() noexcept(false)
    {}

    void operator()(std::exception_ptr) &
    {
    }
};

struct Explicit_move_argument
{
    explicit Explicit_move_argument(int source_value)
        :
            value(source_value)
    {}

    Explicit_move_argument(const Explicit_move_argument&) = delete;
    explicit Explicit_move_argument(Explicit_move_argument&& other)
        :
            value(other.value)
    {}

    int value = 0;
};

struct Throwing_destructor_task
{
    ~Throwing_destructor_task() noexcept(false)
    {}

    void operator()() &
    {
    }
};

struct Explicit_move_result
{
    explicit Explicit_move_result(int source_value)
        :
            value(source_value)
    {}

    Explicit_move_result(const Explicit_move_result&) = delete;
    explicit Explicit_move_result(Explicit_move_result&& other)
        :
            value(other.value)
    {}

    int value = 0;
};

struct Explicit_move_result_task
{
    Explicit_move_result operator()() &
    {
        return Explicit_move_result{91};
    }
};

class Destruction_probe
{
public:
    explicit Destruction_probe(std::atomic_bool* destroyed) noexcept
        :
            m_destroyed(destroyed)
    {}

    Destruction_probe(const Destruction_probe&) = delete;
    Destruction_probe& operator=(const Destruction_probe&) = delete;

    Destruction_probe(Destruction_probe&& other) noexcept
        :
            m_destroyed(std::exchange(other.m_destroyed, nullptr))
    {}

    Destruction_probe& operator=(Destruction_probe&&) = delete;

    ~Destruction_probe()
    {
        if (m_destroyed != nullptr) {
            m_destroyed->store(true, std::memory_order_release);
        }
    }

private:
    std::atomic_bool* m_destroyed;
};

class Submission_probe_task
{
public:
    Submission_probe_task(
        QSemaphore* stored,
        std::atomic_bool* executed,
        std::atomic_bool* destroyed) noexcept
        :
            m_stored(stored),
            m_executed(executed),
            m_destroyed(destroyed)
    {}

    Submission_probe_task(const Submission_probe_task&) = delete;
    Submission_probe_task& operator=(const Submission_probe_task&) = delete;

    Submission_probe_task(Submission_probe_task&& other) noexcept
        :
            m_stored(std::exchange(other.m_stored, nullptr)),
            m_executed(std::exchange(other.m_executed, nullptr)),
            m_destroyed(std::exchange(other.m_destroyed, nullptr))
    {
        if (m_stored != nullptr) {
            m_stored->release();
        }
        m_stored = nullptr;
    }

    Submission_probe_task& operator=(Submission_probe_task&&) = delete;

    ~Submission_probe_task()
    {
        if (m_destroyed != nullptr) {
            m_destroyed->store(true, std::memory_order_release);
        }
    }

    void operator()() &
    {
        m_executed->store(true, std::memory_order_release);
    }

private:
    QSemaphore* m_stored;
    std::atomic_bool* m_executed;
    std::atomic_bool* m_destroyed;
};

struct Throwing_move_void_task
{
    Throwing_move_void_task() = default;
    Throwing_move_void_task(const Throwing_move_void_task&) = delete;
    Throwing_move_void_task& operator=(const Throwing_move_void_task&) = delete;

    Throwing_move_void_task(Throwing_move_void_task&&)
    {
        if (capture_failure_enabled.load(std::memory_order_relaxed)) {
            throw std::runtime_error("capture failed");
        }
    }

    Throwing_move_void_task& operator=(Throwing_move_void_task&&) = delete;

    void operator()() &
    {
    }
};

struct Throwing_copy_void_task
{
    Throwing_copy_void_task() = default;

    Throwing_copy_void_task(const Throwing_copy_void_task&)
    {
        if (capture_failure_enabled.load(std::memory_order_relaxed)) {
            throw std::runtime_error("copy capture failed");
        }
    }

    Throwing_copy_void_task& operator=(const Throwing_copy_void_task&) = delete;
    Throwing_copy_void_task(Throwing_copy_void_task&&) = default;
    Throwing_copy_void_task& operator=(Throwing_copy_void_task&&) = default;

    void operator()() &
    {
    }
};

struct Throwing_member_argument
{
    Throwing_member_argument() = default;
    Throwing_member_argument(const Throwing_member_argument&) = delete;
    Throwing_member_argument& operator=(const Throwing_member_argument&) = delete;

    Throwing_member_argument(Throwing_member_argument&&)
    {
        if (capture_failure_enabled.load(std::memory_order_relaxed)) {
            throw std::runtime_error("member argument capture failed");
        }
    }

    Throwing_member_argument& operator=(Throwing_member_argument&&) = delete;
};

struct Throw_on_second_move_void_task
{
    Throw_on_second_move_void_task(
        std::atomic_int* move_count,
        std::atomic_bool* invoked)
        :
            m_move_count(move_count),
            m_invoked(invoked)
    {
    }

    Throw_on_second_move_void_task(
        const Throw_on_second_move_void_task&) = delete;
    Throw_on_second_move_void_task& operator=(
        const Throw_on_second_move_void_task&) = delete;

    Throw_on_second_move_void_task(
        Throw_on_second_move_void_task&& other)
        :
            m_move_count(other.m_move_count),
            m_invoked(other.m_invoked)
    {
        const int move_number =
            m_move_count->fetch_add(1, std::memory_order_acq_rel) + 1;
        if (move_number == 2) {
            throw std::runtime_error("queued capture move failed");
        }
    }

    Throw_on_second_move_void_task& operator=(
        Throw_on_second_move_void_task&&) = delete;

    void operator()() &
    {
        m_invoked->store(true, std::memory_order_release);
    }

private:
    std::atomic_int* m_move_count;
    std::atomic_bool* m_invoked;
};

template<class Task>
concept Can_post_rvalue_task = requires(QObject* context)
{
    vnm::qt::post_with_exception_reporter(
        context,
        std::declval<Task&&>(),
        nullptr);
};

template<class Task>
concept Can_post_lvalue_task = requires(QObject* context, Task& task)
{
    vnm::qt::post_with_exception_reporter(context, task, nullptr);
};

template<class Reporter>
concept Can_post_with_rvalue_reporter = requires(QObject* context)
{
    vnm::qt::post_with_exception_reporter(
        context,
        Exact_void_lvalue_task{},
        std::declval<Reporter&&>());
};

template<class Task>
concept Can_blocking_call_rvalue_task = requires(QObject* context)
{
    vnm::qt::blocking_call(context, std::declval<Task&&>());
};

static_assert(Can_post_rvalue_task<Exact_void_lvalue_task>);
static_assert(Can_post_lvalue_task<Exact_void_lvalue_task>);
static_assert(!Can_post_rvalue_task<Nonvoid_lvalue_task>);
static_assert(!Can_post_rvalue_task<Rvalue_only_void_task>);
static_assert(Can_post_rvalue_task<Move_only_lvalue_task>);
static_assert(!Can_post_lvalue_task<Move_only_lvalue_task>);
static_assert(Can_post_rvalue_task<Explicit_move_lvalue_task>);
static_assert(!Can_post_rvalue_task<Throwing_destructor_task>);
static_assert(Can_post_with_rvalue_reporter<Explicit_move_reporter>);
static_assert(Can_post_with_rvalue_reporter<Throwing_move_reporter>);
static_assert(!Can_post_with_rvalue_reporter<Throwing_destructor_reporter>);
static_assert(Can_blocking_call_rvalue_task<Exact_void_lvalue_task>);
static_assert(Can_blocking_call_rvalue_task<Explicit_move_lvalue_task>);
static_assert(Can_blocking_call_rvalue_task<Explicit_move_result_task>);
static_assert(!Can_blocking_call_rvalue_task<Rvalue_only_void_task>);
static_assert(!Can_blocking_call_rvalue_task<Throwing_destructor_task>);

class Dispatch_target : public QObject
{
public:
    void record_void_call()
    {
        ++m_execution_count;
        m_last_execution_thread.store(QThread::currentThread(), std::memory_order_release);
    }

    int record_nonvoid_call()
    {
        record_void_call();
        return 43;
    }

    void record_owned_value_and_release(
        std::string value,
        std::string* recorded_value,
        QSemaphore* completion)
    {
        *recorded_value = std::move(value);
        completion->release();
    }

    int consume_value(int value)
    {
        return value + 1;
    }

    int inspect_const_reference(const std::string& value)
    {
        return static_cast<int>(value.size());
    }

    int consume_move_only_argument(std::unique_ptr<int> value)
    {
        return *value + 1;
    }

    int consume_explicit_move_argument(Explicit_move_argument&& value)
    {
        return value.value + 1;
    }

    void consume_explicit_move_argument_void(Explicit_move_argument&& value)
    {
        m_explicit_argument_value.store(value.value, std::memory_order_release);
    }

    int increment_explicit_reference(int& value)
    {
        return ++value;
    }

    int consume_throwing_member_argument(Throwing_member_argument)
    {
        return 1;
    }

    void consume_throwing_member_argument_void(Throwing_member_argument)
    {
    }

    void throw_target_exception()
    {
        throw Target_exception{};
    }

    void const_noop() const
    {
    }

    void accept_explicit_reference(int& value)
    {
        ++value;
    }

    [[nodiscard]] int execution_count() const
    {
        return m_execution_count.load(std::memory_order_acquire);
    }

    [[nodiscard]] QThread* last_execution_thread() const
    {
        return m_last_execution_thread.load(std::memory_order_acquire);
    }

    [[nodiscard]] int explicit_argument_value() const
    {
        return m_explicit_argument_value.load(std::memory_order_acquire);
    }

private:
    std::atomic_int m_execution_count = 0;
    std::atomic<QThread*> m_last_execution_thread = nullptr;
    std::atomic_int m_explicit_argument_value = 0;
};

template<class Obj, class Method, class... Args>
concept Can_post_member_on = requires(
    Obj* target,
    Method method,
    Args&&... args)
{
    vnm::qt::post_with_exception_reporter(
        target,
        method,
        nullptr,
        std::forward<Args>(args)...);
};

template<class Method, class... Args>
concept Can_post_member =
    Can_post_member_on<Dispatch_target, Method, Args...>;

template<class Obj, class Method, class... Args>
concept Can_blocking_call_member_on = requires(
    Obj* target,
    Method method,
    Args&&... args)
{
    vnm::qt::blocking_call(
        target,
        method,
        std::forward<Args>(args)...);
};

static_assert(Can_post_member<decltype(&Dispatch_target::record_void_call)>);
static_assert(!Can_post_member<decltype(&Dispatch_target::record_nonvoid_call)>);
static_assert(Can_post_member<decltype(&Dispatch_target::const_noop)>);
static_assert(!Can_post_member_on<
    const Dispatch_target,
    decltype(&Dispatch_target::const_noop)>);
static_assert(!Can_blocking_call_member_on<
    const Dispatch_target,
    decltype(&Dispatch_target::const_noop)>);
static_assert(!Can_post_member<
    decltype(&Dispatch_target::accept_explicit_reference),
    int&>);
static_assert(Can_post_member<
    decltype(&Dispatch_target::accept_explicit_reference),
    std::reference_wrapper<int>>);
static_assert(Can_post_member<
    decltype(&Dispatch_target::consume_explicit_move_argument_void),
    Explicit_move_argument>);
static_assert(Can_blocking_call_member_on<
    Dispatch_target,
    decltype(&Dispatch_target::consume_explicit_move_argument),
    Explicit_move_argument>);

struct Reporter_state
{
    std::atomic_int target_exception_count = 0;
    QSemaphore reported;
};

std::atomic<Reporter_state*> active_reporter_state = nullptr;

void record_target_exception(std::exception_ptr exception)
{
    Reporter_state* state =
        active_reporter_state.load(std::memory_order_acquire);
    if (!state) {
        return;
    }

    try {
        std::rethrow_exception(exception);
    }
    catch (const Target_exception&) {
        state->target_exception_count.fetch_add(1, std::memory_order_release);
    }
    catch (...) {
    }

    state->reported.release();
}

void record_target_exception_then_throw(std::exception_ptr exception)
{
    record_target_exception(std::move(exception));
    throw std::runtime_error("reporter exception");
}

class Scoped_reporter_state
{
public:
    explicit Scoped_reporter_state(Reporter_state* state)
    {
        active_reporter_state.store(state, std::memory_order_release);
    }

    ~Scoped_reporter_state()
    {
        active_reporter_state.store(nullptr, std::memory_order_release);
    }

    Scoped_reporter_state(const Scoped_reporter_state&) = delete;
    Scoped_reporter_state& operator=(const Scoped_reporter_state&) = delete;
};

class Scoped_worker_target
{
public:
    Scoped_worker_target()
        :
            m_target(new Dispatch_target)
    {
        QObject::connect(
            &m_thread,
            &QThread::finished,
            m_target,
            &QObject::deleteLater);
        m_target->moveToThread(&m_thread);
        m_thread.start();
    }

    ~Scoped_worker_target()
    {
        m_thread.quit();
        m_thread.wait();
    }

    Scoped_worker_target(const Scoped_worker_target&) = delete;
    Scoped_worker_target& operator=(const Scoped_worker_target&) = delete;

    [[nodiscard]] Dispatch_target* target() const
    {
        return m_target;
    }

    [[nodiscard]] QThread* thread()
    {
        return &m_thread;
    }

private:
    QThread m_thread;
    Dispatch_target* m_target;
};

class Scoped_blocked_worker_target
{
public:
    ~Scoped_blocked_worker_target()
    {
        release_gate();
    }

    Scoped_blocked_worker_target(const Scoped_blocked_worker_target&) = delete;
    Scoped_blocked_worker_target& operator=(const Scoped_blocked_worker_target&) = delete;

    Scoped_blocked_worker_target() = default;

    [[nodiscard]] bool block_event_loop()
    {
        m_gate_queued = QMetaObject::invokeMethod(
            m_worker.target(),
            [this]() {
                m_gate_entered.release();
                m_gate_release.acquire();
                m_gate_exited.release();
            },
            Qt::QueuedConnection);

        if (!m_gate_queued) {
            return false;
        }

        m_gate_entered_observed = m_gate_entered.tryAcquire(1, ASYNC_TIMEOUT_MS);
        return m_gate_entered_observed;
    }

    void release_gate()
    {
        if (!m_gate_queued || m_gate_released) {
            return;
        }

        m_gate_released = true;
        m_gate_release.release();
        m_gate_exited.acquire();
    }

    [[nodiscard]] Dispatch_target* target() const
    {
        return m_worker.target();
    }

    [[nodiscard]] QThread* thread()
    {
        return m_worker.thread();
    }

private:
    Scoped_worker_target m_worker;
    QSemaphore m_gate_entered;
    QSemaphore m_gate_release;
    QSemaphore m_gate_exited;
    bool m_gate_queued = false;
    bool m_gate_entered_observed = false;
    bool m_gate_released = false;
};

bool remove_meta_calls_until_finished(
    QObject* receiver,
    const std::atomic_bool& finished)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < ASYNC_TIMEOUT_MS) {
        QCoreApplication::removePostedEvents(receiver, QEvent::MetaCall);
        if (finished.load(std::memory_order_acquire)) {
            return true;
        }
        QThread::yieldCurrentThread();
    }

    QCoreApplication::removePostedEvents(receiver, QEvent::MetaCall);
    return finished.load(std::memory_order_acquire);
}

class Vnm_qt_dispatch_base_tests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void same_thread_post_is_always_deferred()
    {
        std::atomic_bool executed = false;
        QObject context;

        const auto result =
            vnm::qt::post_with_exception_reporter(
                &context,
                [&]() {
                    executed.store(true, std::memory_order_release);
                },
                nullptr);

        QCOMPARE(result, vnm::qt::Post_result::QUEUED);
        QVERIFY(!executed.load(std::memory_order_acquire));
        QTRY_VERIFY_WITH_TIMEOUT(
            executed.load(std::memory_order_acquire),
            ASYNC_TIMEOUT_MS);
    }

    void post_reports_immediate_admission_failures()
    {
        const auto null_result =
            vnm::qt::post_with_exception_reporter(
                static_cast<QObject*>(nullptr),
                Exact_void_lvalue_task{},
                nullptr);
        QCOMPARE(
            null_result,
            vnm::qt::Post_result::RECEIVER_NULL);

        QObject no_affinity_context;
        no_affinity_context.moveToThread(nullptr);
        QCOMPARE(no_affinity_context.thread(), nullptr);

        const auto no_affinity_result =
            vnm::qt::post_with_exception_reporter(
                &no_affinity_context,
                Exact_void_lvalue_task{},
                nullptr);
        QCOMPARE(
            no_affinity_result,
            vnm::qt::Post_result::NO_THREAD_AFFINITY);

        Dispatch_target* null_member_target = nullptr;
        const auto null_member_result =
            vnm::qt::post_with_exception_reporter(
                null_member_target,
                &Dispatch_target::record_void_call,
                nullptr);
        QCOMPARE(
            null_member_result,
            vnm::qt::Post_result::RECEIVER_NULL);

        Dispatch_target no_affinity_member_target;
        no_affinity_member_target.moveToThread(nullptr);
        QCOMPARE(no_affinity_member_target.thread(), nullptr);

        const auto no_affinity_member_result =
            vnm::qt::post_with_exception_reporter(
                &no_affinity_member_target,
                &Dispatch_target::record_void_call,
                nullptr);
        QCOMPARE(
            no_affinity_member_result,
            vnm::qt::Post_result::NO_THREAD_AFFINITY);

        QObject context;
        Throwing_move_void_task throwing_task;
        const auto submission_result =
            vnm::qt::post_with_exception_reporter(
                &context,
                std::move(throwing_task),
                nullptr);
        QCOMPARE(
            submission_result,
            vnm::qt::Post_result::STORAGE_FAILED);

        Throwing_move_reporter throwing_reporter;
        const auto reporter_storage_result =
            vnm::qt::post_with_exception_reporter(
                &context,
                Exact_void_lvalue_task{},
                std::move(throwing_reporter));
        QCOMPARE(
            reporter_storage_result,
            vnm::qt::Post_result::STORAGE_FAILED);

        std::atomic_int queued_capture_move_count = 0;
        std::atomic_bool queued_capture_invoked = false;
        Throw_on_second_move_void_task queued_capture_task(
            &queued_capture_move_count,
            &queued_capture_invoked);
        const auto queued_capture_result =
            vnm::qt::post_with_exception_reporter(
                &context,
                std::move(queued_capture_task),
                nullptr);
        QCOMPARE(
            queued_capture_result,
            vnm::qt::Post_result::QUEUED);
        QCOMPARE(
            queued_capture_move_count.load(std::memory_order_acquire),
            1);
        QTRY_VERIFY_WITH_TIMEOUT(
            queued_capture_invoked.load(std::memory_order_acquire),
            ASYNC_TIMEOUT_MS);

        Dispatch_target member_context;
        const auto member_submission_result =
            vnm::qt::post_with_exception_reporter(
                &member_context,
                &Dispatch_target::consume_throwing_member_argument_void,
                nullptr,
                Throwing_member_argument{});
        QCOMPARE(
            member_submission_result,
            vnm::qt::Post_result::STORAGE_FAILED);
    }

    void queued_post_is_cancelled_by_legal_context_destruction()
    {
        std::atomic_bool executed = false;
        std::atomic_bool task_destroyed = false;
        auto context = std::make_unique<QObject>();

        const auto result =
            vnm::qt::post_with_exception_reporter(
                context.get(),
                [&, probe = Destruction_probe(&task_destroyed)]() {
                    executed.store(true, std::memory_order_release);
                },
                nullptr);
        QCOMPARE(result, vnm::qt::Post_result::QUEUED);

        context.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents();

        QVERIFY(!executed.load(std::memory_order_acquire));
        QVERIFY(task_destroyed.load(std::memory_order_acquire));
    }

    void post_reports_target_exception()
    {
        Reporter_state reporter_state;
        Scoped_reporter_state reporter_scope(&reporter_state);
        Scoped_worker_target worker;

        const auto callable_result =
            vnm::qt::post_with_exception_reporter(
                worker.target(),
                []() {
                    throw Target_exception{};
                },
                &record_target_exception);
        const auto member_result =
            vnm::qt::post_with_exception_reporter(
                worker.target(),
                &Dispatch_target::throw_target_exception,
                &record_target_exception);

        QCOMPARE(
            callable_result,
            vnm::qt::Post_result::QUEUED);
        QCOMPARE(
            member_result,
            vnm::qt::Post_result::QUEUED);
        QVERIFY(reporter_state.reported.tryAcquire(2, ASYNC_TIMEOUT_MS));
        vnm::qt::blocking_call(
            worker.target(),
            []() {
            });
        QCOMPARE(
            reporter_state.target_exception_count.load(std::memory_order_acquire),
            2);
    }

    void post_accepts_owned_capturing_reporters()
    {
        Reporter_state reporter_state;
        Scoped_worker_target worker;
        auto reporter_token = std::make_unique<int>(41);

        const auto result = vnm::qt::post_with_exception_reporter(
            worker.target(),
            []() {
                throw Target_exception{};
            },
            [token = std::move(reporter_token), &reporter_state](
                std::exception_ptr exception) mutable {
                try {
                    std::rethrow_exception(std::move(exception));
                }
                catch (const Target_exception&) {
                    reporter_state.target_exception_count.fetch_add(
                        *token - 40,
                        std::memory_order_release);
                }
                reporter_state.reported.release();
            });

        QCOMPARE(result, vnm::qt::Post_result::QUEUED);
        QVERIFY(reporter_state.reported.tryAcquire(1, ASYNC_TIMEOUT_MS));
        QCOMPARE(
            reporter_state.target_exception_count.load(std::memory_order_acquire),
            1);
    }

    void post_contains_reporter_exception()
    {
        Reporter_state reporter_state;
        Scoped_reporter_state reporter_scope(&reporter_state);
        Scoped_worker_target worker;

        const auto result =
            vnm::qt::post_with_exception_reporter(
                worker.target(),
                []() {
                    throw Target_exception{};
                },
                &record_target_exception_then_throw);

        QCOMPARE(result, vnm::qt::Post_result::QUEUED);
        QVERIFY(reporter_state.reported.tryAcquire(1, ASYNC_TIMEOUT_MS));

        const int follow_up_result =
            vnm::qt::blocking_call(
                worker.target(),
                []() {
                    return 19;
                });
        QCOMPARE(follow_up_result, 19);
        QCOMPARE(
            reporter_state.target_exception_count.load(std::memory_order_acquire),
            1);
    }

    void post_member_owns_ordinary_arguments()
    {
        std::string recorded_value;
        QSemaphore completion;
        Scoped_worker_target worker;
        std::string source = "owned before mutation";

        const auto result =
            vnm::qt::post_with_exception_reporter(
                worker.target(),
                &Dispatch_target::record_owned_value_and_release,
                nullptr,
                source,
                &recorded_value,
                &completion);
        source = "mutated after admission";

        QCOMPARE(result, vnm::qt::Post_result::QUEUED);
        QVERIFY(completion.tryAcquire(1, ASYNC_TIMEOUT_MS));
        QCOMPARE(recorded_value, std::string("owned before mutation"));
    }

    void blocking_call_uses_actual_affinity_inline_and_cross_thread()
    {
        Dispatch_target inline_target;
        std::atomic<QThread*> inline_execution_thread = nullptr;

        const int inline_result =
            vnm::qt::blocking_call(
                &inline_target,
                [&]() {
                    inline_execution_thread.store(
                        QThread::currentThread(),
                        std::memory_order_release);
                    return 17;
                });
        QCOMPARE(inline_result, 17);
        QCOMPARE(
            inline_execution_thread.load(std::memory_order_acquire),
            QThread::currentThread());

        const int inline_member_result =
            vnm::qt::blocking_call(
                &inline_target,
                &Dispatch_target::record_nonvoid_call);
        QCOMPARE(inline_member_result, 43);
        QCOMPARE(inline_target.execution_count(), 1);
        QCOMPARE(
            inline_target.last_execution_thread(),
            QThread::currentThread());

        Scoped_worker_target worker;
        const int cross_thread_result =
            vnm::qt::blocking_call(
                worker.target(),
                &Dispatch_target::record_nonvoid_call);

        QCOMPARE(cross_thread_result, 43);
        QCOMPARE(worker.target()->execution_count(), 1);
        QCOMPARE(worker.target()->last_execution_thread(), worker.thread());
    }

    void blocking_call_propagates_original_target_exception()
    {
        QObject inline_context;
        bool inline_target_exception = false;
        bool inline_unexpected_exception = false;
        try {
            vnm::qt::blocking_call(
                &inline_context,
                []() {
                    throw Target_exception{};
                });
        }
        catch (const Target_exception&) {
            inline_target_exception = true;
        }
        catch (...) {
            inline_unexpected_exception = true;
        }

        QVERIFY(inline_target_exception);
        QVERIFY(!inline_unexpected_exception);

        Scoped_worker_target worker;
        bool cross_thread_target_exception = false;
        bool cross_thread_unexpected_exception = false;
        try {
            vnm::qt::blocking_call(
                worker.target(),
                &Dispatch_target::throw_target_exception);
        }
        catch (const Target_exception&) {
            cross_thread_target_exception = true;
        }
        catch (...) {
            cross_thread_unexpected_exception = true;
        }

        QVERIFY(cross_thread_target_exception);
        QVERIFY(!cross_thread_unexpected_exception);
    }

    void blocking_call_preserves_target_future_error()
    {
        Scoped_worker_target worker;
        bool caught_original = false;
        bool caught_dispatch_error = false;

        try {
            vnm::qt::blocking_call(
                worker.target(),
                []() -> void {
                    throw std::future_error(
                        std::future_errc::broken_promise);
                });
        }
        catch (const std::future_error& error) {
            caught_original =
                error.code() ==
                std::make_error_code(std::future_errc::broken_promise);
        }
        catch (const vnm::qt::Dispatch_error&) {
            caught_dispatch_error = true;
        }

        QVERIFY(caught_original);
        QVERIFY(!caught_dispatch_error);
    }

    void blocking_call_supports_move_only_results()
    {
        Scoped_worker_target worker;

        auto result =
            vnm::qt::blocking_call(
                worker.target(),
                []() {
                    return std::make_unique<int>(83);
                });

        QVERIFY(result != nullptr);
        QCOMPARE(*result, 83);
    }

    void blocking_call_supports_explicit_moves_and_destroys_task_before_return()
    {
        Scoped_worker_target worker;

        auto explicit_move_result = vnm::qt::blocking_call(
            worker.target(),
            Explicit_move_result_task{});
        QCOMPARE(explicit_move_result.value, 91);

        std::atomic_bool task_destroyed = false;
        const int result = vnm::qt::blocking_call(
            worker.target(),
            [probe = Destruction_probe(&task_destroyed)]() {
                return 29;
            });

        QCOMPARE(result, 29);
        QVERIFY(task_destroyed.load(std::memory_order_acquire));
    }

    void member_binder_supports_owned_and_explicit_reference_forms()
    {
        Scoped_worker_target worker;

        const int value_result =
            vnm::qt::blocking_call(
                worker.target(),
                &Dispatch_target::consume_value,
                40);
        QCOMPARE(value_result, 41);

        std::string text = "const reference";
        const int const_reference_result =
            vnm::qt::blocking_call(
                worker.target(),
                &Dispatch_target::inspect_const_reference,
                text);
        QCOMPARE(
            const_reference_result,
            static_cast<int>(text.size()));

        const int move_only_result =
            vnm::qt::blocking_call(
                worker.target(),
                &Dispatch_target::consume_move_only_argument,
                std::make_unique<int>(50));
        QCOMPARE(move_only_result, 51);

        const int explicit_move_result =
            vnm::qt::blocking_call(
                worker.target(),
                &Dispatch_target::consume_explicit_move_argument,
                Explicit_move_argument{60});
        QCOMPARE(explicit_move_result, 61);

        const auto explicit_move_post_result =
            vnm::qt::post_with_exception_reporter(
                worker.target(),
                &Dispatch_target::consume_explicit_move_argument_void,
                nullptr,
                Explicit_move_argument{62});
        QCOMPARE(
            explicit_move_post_result,
            vnm::qt::Post_result::QUEUED);
        vnm::qt::blocking_call(worker.target(), [] {});
        QCOMPARE(worker.target()->explicit_argument_value(), 62);

        int explicit_reference_value = 70;
        const int explicit_reference_result =
            vnm::qt::blocking_call(
                worker.target(),
                &Dispatch_target::increment_explicit_reference,
                std::ref(explicit_reference_value));
        QCOMPARE(explicit_reference_result, 71);
        QCOMPARE(explicit_reference_value, 71);
    }

    void blocking_call_reports_receiver_errors_and_preserves_setup_exceptions()
    {
        bool caught_null = false;
        bool caught_unexpected_null = false;
        try {
            vnm::qt::blocking_call(
                static_cast<QObject*>(nullptr),
                Exact_void_lvalue_task{});
        }
        catch (const vnm::qt::Dispatch_error& error) {
            caught_null =
                error.code() ==
                vnm::qt::Dispatch_errc::RECEIVER_NULL;
        }
        catch (...) {
            caught_unexpected_null = true;
        }
        QVERIFY(caught_null);
        QVERIFY(!caught_unexpected_null);

        Dispatch_target* null_member_target = nullptr;
        bool caught_null_member = false;
        try {
            (void)vnm::qt::blocking_call(
                null_member_target,
                &Dispatch_target::record_nonvoid_call);
        }
        catch (const vnm::qt::Dispatch_error& error) {
            caught_null_member =
                error.code() ==
                vnm::qt::Dispatch_errc::RECEIVER_NULL;
        }
        QVERIFY(caught_null_member);

        QObject no_affinity_context;
        no_affinity_context.moveToThread(nullptr);
        QCOMPARE(no_affinity_context.thread(), nullptr);

        bool caught_no_affinity = false;
        bool caught_unexpected_no_affinity = false;
        try {
            vnm::qt::blocking_call(
                &no_affinity_context,
                Exact_void_lvalue_task{});
        }
        catch (const vnm::qt::Dispatch_error& error) {
            caught_no_affinity =
                error.code() ==
                vnm::qt::Dispatch_errc::NO_THREAD_AFFINITY;
        }
        catch (...) {
            caught_unexpected_no_affinity = true;
        }
        QVERIFY(caught_no_affinity);
        QVERIFY(!caught_unexpected_no_affinity);

        Dispatch_target no_affinity_member_target;
        no_affinity_member_target.moveToThread(nullptr);
        QCOMPARE(no_affinity_member_target.thread(), nullptr);

        bool caught_no_affinity_member = false;
        try {
            (void)vnm::qt::blocking_call(
                &no_affinity_member_target,
                &Dispatch_target::record_nonvoid_call);
        }
        catch (const vnm::qt::Dispatch_error& error) {
            caught_no_affinity_member =
                error.code() ==
                vnm::qt::Dispatch_errc::NO_THREAD_AFFINITY;
        }
        QVERIFY(caught_no_affinity_member);

        QObject context;
        Throwing_move_void_task throwing_task;
        bool caught_original_move_exception = false;
        try {
            vnm::qt::blocking_call(
                &context,
                std::move(throwing_task));
        }
        catch (const std::runtime_error& error) {
            caught_original_move_exception =
                std::string(error.what()) == "capture failed";
        }
        QVERIFY(caught_original_move_exception);

        Throwing_copy_void_task throwing_copy_task;
        bool caught_original_copy_exception = false;
        try {
            vnm::qt::blocking_call(
                &context,
                throwing_copy_task);
        }
        catch (const std::runtime_error& error) {
            caught_original_copy_exception =
                std::string(error.what()) == "copy capture failed";
        }
        QVERIFY(caught_original_copy_exception);

        std::atomic_int queued_capture_move_count = 0;
        std::atomic_bool queued_capture_invoked = false;
        Scoped_worker_target queued_capture_worker;
        Throw_on_second_move_void_task queued_capture_task(
            &queued_capture_move_count,
            &queued_capture_invoked);

        vnm::qt::blocking_call(
            queued_capture_worker.target(),
            std::move(queued_capture_task));

        QCOMPARE(
            queued_capture_move_count.load(std::memory_order_acquire),
            1);
        QVERIFY(queued_capture_invoked.load(std::memory_order_acquire));

        Dispatch_target member_context;
        bool caught_original_member_exception = false;
        try {
            (void)vnm::qt::blocking_call(
                &member_context,
                &Dispatch_target::consume_throwing_member_argument,
                Throwing_member_argument{});
        }
        catch (const std::runtime_error& error) {
            caught_original_member_exception =
                std::string(error.what()) ==
                "member argument capture failed";
        }
        QVERIFY(caught_original_member_exception);
    }

    void blocking_call_void_callable_reports_typed_cancellation()
    {
        std::atomic_bool callable_executed = false;
        std::atomic_bool task_destroyed = false;
        std::atomic_bool caller_finished = false;
        bool caught_cancellation = false;
        bool caught_unexpected_exception = false;
        Scoped_blocked_worker_target worker;
        QVERIFY(worker.block_event_loop());

        std::thread caller([&]() {
            try {
                vnm::qt::blocking_call(
                    worker.target(),
                    [&, probe = Destruction_probe(&task_destroyed)]() {
                        callable_executed.store(
                            true,
                            std::memory_order_release);
                    });
            }
            catch (const vnm::qt::Dispatch_error& error) {
                caught_cancellation =
                    error.code() ==
                    vnm::qt::Dispatch_errc::CANCELLED_BEFORE_EXECUTION;
            }
            catch (...) {
                caught_unexpected_exception = true;
            }
            caller_finished.store(true, std::memory_order_release);
        });

        const bool finished_before_gate_release =
            remove_meta_calls_until_finished(worker.target(), caller_finished);
        if (!finished_before_gate_release) {
            worker.release_gate();
        }
        caller.join();
        worker.release_gate();

        QVERIFY2(
            finished_before_gate_release,
            "blocking_call() did not unblock after its void callable meta-call was removed.");
        QVERIFY(caught_cancellation);
        QVERIFY(!caught_unexpected_exception);
        QVERIFY(!callable_executed.load(std::memory_order_acquire));
        QVERIFY(task_destroyed.load(std::memory_order_acquire));
    }

    void blocking_call_nonvoid_callable_reports_typed_cancellation()
    {
        std::atomic_bool callable_executed = false;
        std::atomic_bool caller_finished = false;
        bool caught_cancellation = false;
        bool caught_unexpected_exception = false;
        Scoped_blocked_worker_target worker;
        QVERIFY(worker.block_event_loop());

        std::thread caller([&]() {
            try {
                (void)vnm::qt::blocking_call(
                    worker.target(),
                    [&]() {
                        callable_executed.store(
                            true,
                            std::memory_order_release);
                        return 31;
                    });
            }
            catch (const vnm::qt::Dispatch_error& error) {
                caught_cancellation =
                    error.code() ==
                    vnm::qt::Dispatch_errc::CANCELLED_BEFORE_EXECUTION;
            }
            catch (...) {
                caught_unexpected_exception = true;
            }
            caller_finished.store(true, std::memory_order_release);
        });

        const bool finished_before_gate_release =
            remove_meta_calls_until_finished(worker.target(), caller_finished);
        if (!finished_before_gate_release) {
            worker.release_gate();
        }
        caller.join();
        worker.release_gate();

        QVERIFY2(
            finished_before_gate_release,
            "blocking_call() did not unblock after its non-void callable meta-call was removed.");
        QVERIFY(caught_cancellation);
        QVERIFY(!caught_unexpected_exception);
        QVERIFY(!callable_executed.load(std::memory_order_acquire));
    }

    void blocking_call_member_reports_typed_cancellation()
    {
        std::atomic_bool caller_finished = false;
        bool caught_cancellation = false;
        bool caught_unexpected_exception = false;
        Scoped_blocked_worker_target worker;
        QVERIFY(worker.block_event_loop());

        std::thread caller([&]() {
            try {
                (void)vnm::qt::blocking_call(
                    worker.target(),
                    &Dispatch_target::record_nonvoid_call);
            }
            catch (const vnm::qt::Dispatch_error& error) {
                caught_cancellation =
                    error.code() ==
                    vnm::qt::Dispatch_errc::CANCELLED_BEFORE_EXECUTION;
            }
            catch (...) {
                caught_unexpected_exception = true;
            }
            caller_finished.store(true, std::memory_order_release);
        });

        const bool finished_before_gate_release =
            remove_meta_calls_until_finished(worker.target(), caller_finished);
        if (!finished_before_gate_release) {
            worker.release_gate();
        }
        caller.join();
        worker.release_gate();

        QVERIFY2(
            finished_before_gate_release,
            "blocking_call() did not unblock after its bound member meta-call was removed.");
        QVERIFY(caught_cancellation);
        QVERIFY(!caught_unexpected_exception);
        QCOMPARE(worker.target()->execution_count(), 0);
    }

    void blocking_call_reports_receiver_destruction_cancellation()
    {
        QSemaphore task_stored;
        std::atomic_bool task_executed = false;
        std::atomic_bool task_destroyed = false;
        std::atomic_bool receiver_destroyed = false;
        bool caught_cancellation = false;
        bool caught_unexpected_exception = false;
        Scoped_blocked_worker_target worker;
        QVERIFY(worker.block_event_loop());

        Dispatch_target* const target = worker.target();
        auto* const deleter = new QObject;
        QObject::connect(
            worker.thread(),
            &QThread::finished,
            deleter,
            &QObject::deleteLater);
        deleter->moveToThread(worker.thread());

        const bool deletion_queued = QMetaObject::invokeMethod(
            deleter,
            [target, &receiver_destroyed]() {
                delete target;
                receiver_destroyed.store(true, std::memory_order_release);
            },
            Qt::QueuedConnection);
        QVERIFY(deletion_queued);

        Submission_probe_task task(
            &task_stored,
            &task_executed,
            &task_destroyed);
        std::thread caller([&]() {
            try {
                vnm::qt::blocking_call(target, std::move(task));
            }
            catch (const vnm::qt::Dispatch_error& error) {
                caught_cancellation =
                    error.code() ==
                    vnm::qt::Dispatch_errc::CANCELLED_BEFORE_EXECUTION;
            }
            catch (...) {
                caught_unexpected_exception = true;
            }
        });

        const bool stored =
            task_stored.tryAcquire(1, ASYNC_TIMEOUT_MS);
        if (stored) {
            // The receiver thread remains gated while the caller completes the
            // immediately following Qt submission.
            QTest::qWait(50);
        }
        worker.release_gate();
        caller.join();

        QVERIFY2(stored, "blocking_call() did not finish storing its task.");
        QVERIFY(receiver_destroyed.load(std::memory_order_acquire));
        QVERIFY(caught_cancellation);
        QVERIFY(!caught_unexpected_exception);
        QVERIFY(!task_executed.load(std::memory_order_acquire));
        QVERIFY(task_destroyed.load(std::memory_order_acquire));
    }

};

} // namespace

QTEST_GUILESS_MAIN(Vnm_qt_dispatch_base_tests)

#include "vnm_qt_dispatch_tests.moc"
