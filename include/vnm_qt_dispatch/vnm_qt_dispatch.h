#pragma once

#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QtGlobal>

#include <concepts>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
#  error "vnm_qt_dispatch requires Qt 6.5 or newer."
#endif

#ifdef QT_NO_EXCEPTIONS
#  error "vnm_qt_dispatch requires C++ exception support."
#endif

/**
 * @file vnm_qt_dispatch.h
 * @brief Owned and exception-contained dispatch through QObject affinity.
 *
 * `post()` always queues exact-void work. `blocking_call()` executes inline when
 * the receiver belongs to the calling thread and otherwise queues the work and
 * waits. Callable objects and ordinary typed-member arguments are decayed and
 * owned as values; member arguments are consumed once. Decaying a pointer or a
 * view does not own its referent. Use `std::ref()` only for deliberate reference
 * semantics.
 *
 * The library does not make a raw `QObject*` lifetime-safe. The receiver must
 * remain valid, and its thread affinity must not change concurrently, while an
 * operation inspects and submits through that pointer. A cross-thread blocking
 * call additionally requires the queued event either to run or to be destroyed;
 * otherwise its wait is intentionally unbounded.
 *
 * Stored tasks, reporters, arguments, results, and exception objects can be
 * destroyed on the submitting thread, receiver thread, or a thread removing
 * posted events. Their destruction must therefore be non-throwing and
 * thread-agnostic. The concepts below enforce non-throwing destruction where
 * the C++ type system can express it.
 */

namespace vnm {
namespace qt {

enum class Post_result
{
    QUEUED,
    RECEIVER_NULL,
    NO_THREAD_AFFINITY,
    SUBMISSION_FAILED,
    STORAGE_FAILED,
};

enum class Dispatch_errc
{
    RECEIVER_NULL,
    NO_THREAD_AFFINITY,
    SUBMISSION_FAILED,
    CANCELLED_BEFORE_EXECUTION,
};

class Dispatch_error : public std::runtime_error
{
public:
    Dispatch_error(Dispatch_errc error_code, const char* message)
        :
            std::runtime_error(message),
            m_error_code(error_code)
    {}

    [[nodiscard]] Dispatch_errc code() const noexcept
    {
        return m_error_code;
    }

private:
    Dispatch_errc m_error_code;
};

/** Explicit policy for intentionally discarding exceptions from posted work. */
struct Ignore_exceptions
{
    void operator()(std::exception_ptr) const noexcept
    {}
};

inline constexpr Ignore_exceptions ignore_exceptions{};

namespace detail {

struct Qt_warning_reporter
{
    void operator()(std::exception_ptr exception) const noexcept
    {
        if (!exception) {
            return;
        }

        try {
            try {
                std::rethrow_exception(exception);
            }
            catch (const std::exception& error) {
                qWarning(
                    "vnm::qt::post: Queued target threw an exception: %s",
                    error.what());
            }
            catch (...) {
                qWarning(
                    "vnm::qt::post: Queued target threw an unknown exception.");
            }
        }
        catch (...) {
            // Diagnostics must not escape through Qt event delivery.
        }
    }
};

inline constexpr Qt_warning_reporter qt_warning_reporter{};

template<class Source>
using stored_t = std::decay_t<Source>;

template<class TaskSource>
concept Storable_task =
    std::constructible_from<stored_t<TaskSource>, TaskSource&&>;

template<class TaskSource>
concept Invocable_task =
    Storable_task<TaskSource> &&
    std::invocable<stored_t<TaskSource>&>;

template<class TaskSource>
concept Exact_void_task =
    Invocable_task<TaskSource> &&
    std::same_as<std::invoke_result_t<stored_t<TaskSource>&>, void>;

template<class Result>
concept Storable_result =
    std::is_void_v<Result> ||
    (!std::is_reference_v<Result> &&
     std::constructible_from<Result, Result&&>);

template<class TaskSource>
concept Blocking_task =
    Invocable_task<TaskSource> &&
    Storable_result<std::invoke_result_t<stored_t<TaskSource>&>>;

template<class Observer>
concept Submission_observer =
    std::invocable<Observer&> &&
    std::same_as<std::invoke_result_t<Observer&>, void> &&
    std::is_nothrow_invocable_v<Observer&>;

struct No_submission_observer
{
    void operator()() const noexcept
    {}
};

inline constexpr No_submission_observer no_submission_observer{};

template<class ReporterSource>
concept Storable_reporter =
    std::constructible_from<stored_t<ReporterSource>, ReporterSource&&> &&
    (std::same_as<stored_t<ReporterSource>, std::nullptr_t> ||
     std::invocable<stored_t<ReporterSource>&, std::exception_ptr>);

template<class Obj, class Method, class... Args>
concept Storable_member_call =
    std::same_as<Obj, std::remove_cv_t<Obj>> &&
    std::derived_from<Obj, QObject> &&
    std::is_member_function_pointer_v<Method> &&
    (std::constructible_from<std::decay_t<Args>, Args&&> && ...) &&
    std::invocable<Method, Obj*, std::decay_t<Args>&&...>;

template<class Obj, class Method, class... Args>
concept Exact_void_member_call =
    Storable_member_call<Obj, Method, Args...> &&
    std::same_as<
        std::invoke_result_t<Method, Obj*, std::decay_t<Args>&&...>,
        void>;

template<class Obj, class Method, class... Args>
concept Blocking_member_call =
    Storable_member_call<Obj, Method, Args...> &&
    Storable_result<
        std::invoke_result_t<Method, Obj*, std::decay_t<Args>&&...>>;

template<class Obj, class Method, class... StoredArgs>
class Member_task
{
public:
    template<class... Args>
    explicit Member_task(Obj* object, Method method, Args&&... args)
        :
            m_object(object),
            m_method(method),
            m_arguments(std::forward<Args>(args)...)
    {}

    decltype(auto) operator()() &
    {
        return std::apply(
            [this](auto&... arguments) -> decltype(auto)
            {
                return std::invoke(
                    m_method,
                    m_object,
                    std::move(arguments)...);
            },
            m_arguments);
    }

private:
    Obj* m_object;
    Method m_method;
    std::tuple<StoredArgs...> m_arguments;
};

template<class Obj, class Method, class... Args>
using member_task_t =
    Member_task<Obj, Method, std::decay_t<Args>...>;

template<class Reporter>
[[nodiscard]] bool is_null_reporter(const Reporter& reporter) noexcept
{
    if constexpr (std::same_as<Reporter, std::nullptr_t>) {
        return true;
    }
    else if constexpr (std::is_pointer_v<Reporter>) {
        return reporter == nullptr;
    }
    else {
        return false;
    }
}

template<class Task, class Reporter>
class Post_operation
{
public:
    template<class ReporterSource, class... TaskArgs>
    explicit Post_operation(
        std::in_place_t,
        ReporterSource&& reporter,
        TaskArgs&&... task_args)
        :
            m_task(std::forward<TaskArgs>(task_args)...),
            m_reporter(std::forward<ReporterSource>(reporter))
    {}

    void execute() noexcept
    {
        try {
            std::invoke(m_task);
        }
        catch (...) {
            if (is_null_reporter(m_reporter)) {
                return;
            }

            if constexpr (!std::same_as<Reporter, std::nullptr_t>) {
                try {
                    std::invoke(m_reporter, std::current_exception());
                }
                catch (...) {
                    // No exception may escape through Qt event delivery.
                }
            }
        }
    }

private:
    Task m_task;
    [[no_unique_address]] Reporter m_reporter;
};

template<class Task, class ReporterSource, class... TaskArgs>
[[nodiscard]] Post_result post_impl(
    QObject* context,
    ReporterSource&& reporter,
    TaskArgs&&... task_args) noexcept
{
    if (context == nullptr) {
        return Post_result::RECEIVER_NULL;
    }
    try {
        if (context->thread() == nullptr) {
            return Post_result::NO_THREAD_AFFINITY;
        }
    }
    catch (...) {
        return Post_result::SUBMISSION_FAILED;
    }

    using reporter_t = stored_t<ReporterSource>;
    using operation_t = Post_operation<Task, reporter_t>;

    std::unique_ptr<operation_t> operation;
    try {
        operation = std::make_unique<operation_t>(
            std::in_place,
            std::forward<ReporterSource>(reporter),
            std::forward<TaskArgs>(task_args)...);
    }
    catch (...) {
        return Post_result::STORAGE_FAILED;
    }

    try {
        const bool accepted = QMetaObject::invokeMethod(
            context,
            [operation = std::move(operation)]() noexcept
            {
                operation->execute();
            },
            Qt::QueuedConnection);

        return accepted
            ? Post_result::QUEUED
            : Post_result::SUBMISSION_FAILED;
    }
    catch (...) {
        return Post_result::SUBMISSION_FAILED;
    }
}

[[nodiscard]] inline bool is_current_thread(
    const QThread* target_thread) noexcept
{
    if (target_thread == nullptr) {
        return false;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    return target_thread->isCurrentThread();
#else
    return QThread::currentThread() == target_thread;
#endif
}

template<class Result>
class Call_result
{
public:
    template<class Value>
    void emplace(Value&& value)
    {
        m_value.emplace(std::forward<Value>(value));
    }

    [[nodiscard]] Result take()
    {
        if (!m_value) {
            throw std::logic_error(
                "Qt blocking dispatch completed without a result.");
        }
        return Result(std::move(*m_value));
    }

private:
    std::optional<Result> m_value;
};

template<>
class Call_result<void>
{
public:
    void take() const noexcept
    {}
};

template<class Task, class Result>
class Call_operation
{
public:
    template<class... TaskArgs>
    explicit Call_operation(std::in_place_t, TaskArgs&&... task_args)
        :
            m_task(std::in_place, std::forward<TaskArgs>(task_args)...)
    {}

    void execute() noexcept
    {
        try {
            if (!m_task) {
                throw std::logic_error(
                    "Qt blocking dispatch started without a task.");
            }

            if constexpr (std::is_void_v<Result>) {
                std::invoke(*m_task);
            }
            else {
                m_result.emplace(std::invoke(*m_task));
            }
        }
        catch (...) {
            m_exception = std::current_exception();
        }

        // Task destruction is part of synchronous completion.
        m_task.reset();
    }

    void cancel() noexcept
    {
        // Called before an abandoned promise wakes the waiting thread.
        m_task.reset();
    }

    [[nodiscard]] Result take()
    {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }

        if constexpr (std::is_void_v<Result>) {
            m_result.take();
            return;
        }
        else {
            return m_result.take();
        }
    }

private:
    std::optional<Task> m_task;
    Call_result<Result> m_result;
    std::exception_ptr m_exception;
};

/**
 * Move-only queued functor. If Qt destroys it without invoking it, its promise
 * is abandoned and the waiting future becomes ready with `broken_promise`.
 */
template<class Operation>
class Call_event
{
public:
    explicit Call_event(std::shared_ptr<Operation> operation)
        :
            m_operation(std::move(operation))
    {}

    Call_event(const Call_event&) = delete;
    Call_event& operator=(const Call_event&) = delete;

    Call_event(Call_event&& other) noexcept
        :
            m_operation(std::move(other.m_operation)),
            m_completion(std::move(other.m_completion)),
            m_armed(std::exchange(other.m_armed, false))
    {}

    Call_event& operator=(Call_event&&) = delete;

    ~Call_event() noexcept
    {
        if (m_armed && m_operation) {
            // Promise destruction follows this body, so task destruction is
            // sequenced before broken_promise wakes the caller.
            m_operation->cancel();
            m_operation.reset();
        }
    }

    [[nodiscard]] std::future<void> get_future()
    {
        return m_completion.get_future();
    }

    void operator()() noexcept
    {
        m_operation->execute();
        m_operation.reset();
        m_armed = false;
        try {
            m_completion.set_value();
        }
        catch (...) {
            // Promise destruction still releases the waiter. Nothing may
            // escape through Qt event delivery.
        }
    }

private:
    std::shared_ptr<Operation> m_operation;
    std::promise<void> m_completion;
    bool m_armed = true;
};

template<class Task, class SubmissionObserver, class... TaskArgs>
requires Submission_observer<SubmissionObserver>
[[nodiscard]] auto blocking_call_impl(
    QObject* context,
    SubmissionObserver&& submission_observer,
    TaskArgs&&... task_args)
    -> std::invoke_result_t<Task&>
{
    using result_t = std::invoke_result_t<Task&>;

    if (context == nullptr) {
        throw Dispatch_error(
            Dispatch_errc::RECEIVER_NULL,
            "Qt dispatch receiver is null.");
    }

    QThread* const target_thread = context->thread();
    if (target_thread == nullptr) {
        throw Dispatch_error(
            Dispatch_errc::NO_THREAD_AFFINITY,
            "Qt dispatch receiver has no thread affinity.");
    }

    // Setup failures retain their original exception type.
    if (is_current_thread(target_thread)) {
        Task task(std::forward<TaskArgs>(task_args)...);
        std::invoke(submission_observer);
        if constexpr (std::is_void_v<result_t>) {
            std::invoke(task);
            return;
        }
        else {
            return std::invoke(task);
        }
    }

    using operation_t = Call_operation<Task, result_t>;
    auto operation = std::make_shared<operation_t>(
        std::in_place,
        std::forward<TaskArgs>(task_args)...);

    Call_event<operation_t> event(operation);
    std::future<void> completion = event.get_future();

    const bool accepted = QMetaObject::invokeMethod(
        context,
        std::move(event),
        Qt::QueuedConnection);

    if (!accepted) {
        throw Dispatch_error(
            Dispatch_errc::SUBMISSION_FAILED,
            "Qt rejected the blocking dispatch.");
    }

    std::invoke(submission_observer);

    try {
        completion.get();
    }
    catch (const std::future_error& error) {
        if (error.code() ==
            std::make_error_code(std::future_errc::broken_promise)) {
            throw Dispatch_error(
                Dispatch_errc::CANCELLED_BEFORE_EXECUTION,
                "Qt dispatch was cancelled before execution.");
        }
        throw;
    }

    if constexpr (std::is_void_v<result_t>) {
        operation->take();
        return;
    }
    else {
        return operation->take();
    }
}

} // namespace detail

/**
 * Always enqueue an exact-void task with an explicit exception reporter.
 *
 * `QUEUED` means that Qt accepted the event, not that it executed. Target and
 * reporter exceptions are contained. Passing `nullptr` or a null function
 * pointer suppresses target-exception reporting; `ignore_exceptions` is the
 * clearer named policy. Task/reporter construction or allocation failure
 * returns `STORAGE_FAILED`.
 */
template<class TaskSource, class ReporterSource>
requires
    detail::Exact_void_task<TaskSource> &&
    detail::Storable_reporter<ReporterSource>
[[nodiscard(
    "post_with_exception_reporter() reports admission only; posted work can still be cancelled")]]
Post_result post_with_exception_reporter(
    QObject* context,
    TaskSource&& task,
    ReporterSource&& reporter) noexcept
{
    using task_t = detail::stored_t<TaskSource>;
    return detail::post_impl<task_t>(
        context,
        std::forward<ReporterSource>(reporter),
        std::forward<TaskSource>(task));
}

/** Always enqueue an exact-void task and report target exceptions via qWarning(). */
template<class TaskSource>
requires detail::Exact_void_task<TaskSource>
[[nodiscard(
    "vnm::qt::post() reports admission only; posted work can still be cancelled")]]
Post_result post(QObject* context, TaskSource&& task) noexcept
{
    return post_with_exception_reporter(
        context,
        std::forward<TaskSource>(task),
        detail::qt_warning_reporter);
}

/**
 * Invoke a task synchronously according to the receiver's thread affinity.
 *
 * Same-thread work executes inline. Cross-thread work is queued and the caller
 * waits without a timeout. The original setup, target, result-storage, or
 * result-transfer exception is propagated. Destruction or removal of the
 * queued event before execution throws
 * `Dispatch_error{CANCELLED_BEFORE_EXECUTION}`.
 */
template<class TaskSource>
requires detail::Blocking_task<TaskSource>
[[nodiscard]] auto blocking_call(QObject* context, TaskSource&& task)
    -> std::invoke_result_t<detail::stored_t<TaskSource>&>
{
    using task_t = detail::stored_t<TaskSource>;
    return detail::blocking_call_impl<task_t>(
        context,
        detail::no_submission_observer,
        std::forward<TaskSource>(task));
}

/**
 * Invoke a task synchronously and observe the point when dispatch is committed.
 *
 * The noexcept observer runs on the calling thread. Same-thread calls invoke it
 * immediately before the task. Cross-thread calls invoke it after Qt accepts
 * the queued event and before waiting for completion. It is not invoked when
 * receiver inspection, task storage, or event submission fails.
 */
template<class TaskSource, class SubmissionObserver>
requires
    detail::Blocking_task<TaskSource> &&
    detail::Submission_observer<SubmissionObserver>
[[nodiscard]] auto blocking_call_with_submission_observer(
    QObject* context,
    TaskSource&& task,
    SubmissionObserver&& submission_observer)
    -> std::invoke_result_t<detail::stored_t<TaskSource>&>
{
    using task_t = detail::stored_t<TaskSource>;
    return detail::blocking_call_impl<task_t>(
        context,
        std::forward<SubmissionObserver>(submission_observer),
        std::forward<TaskSource>(task));
}

/** Enqueue a typed exact-void member call with owned, one-shot arguments. */
template<class Obj, class Method, class ReporterSource, class... Args>
requires
    detail::Exact_void_member_call<Obj, Method, Args...> &&
    detail::Storable_reporter<ReporterSource>
[[nodiscard(
    "post_with_exception_reporter() reports admission only; posted work can still be cancelled")]]
Post_result post_with_exception_reporter(
    Obj* object,
    Method method,
    ReporterSource&& reporter,
    Args&&... args) noexcept
{
    using task_t = detail::member_task_t<Obj, Method, Args...>;
    return detail::post_impl<task_t>(
        static_cast<QObject*>(object),
        std::forward<ReporterSource>(reporter),
        object,
        method,
        std::forward<Args>(args)...);
}

/**
 * Enqueue a typed exact-void member call with owned, one-shot arguments and
 * qWarning() target-exception reporting.
 */
template<class Obj, class Method, class... Args>
requires detail::Exact_void_member_call<Obj, Method, Args...>
[[nodiscard(
    "vnm::qt::post() reports admission only; posted work can still be cancelled")]]
Post_result post(Obj* object, Method method, Args&&... args) noexcept
{
    return post_with_exception_reporter(
        object,
        method,
        detail::qt_warning_reporter,
        std::forward<Args>(args)...);
}

/** Invoke a typed member call synchronously with owned, one-shot arguments. */
template<class Obj, class Method, class... Args>
requires detail::Blocking_member_call<Obj, Method, Args...>
[[nodiscard]] auto blocking_call(
    Obj* object,
    Method method,
    Args&&... args)
    -> std::invoke_result_t<Method, Obj*, std::decay_t<Args>&&...>
{
    using task_t = detail::member_task_t<Obj, Method, Args...>;
    return detail::blocking_call_impl<task_t>(
        static_cast<QObject*>(object),
        detail::no_submission_observer,
        object,
        method,
        std::forward<Args>(args)...);
}

} // namespace qt
} // namespace vnm
