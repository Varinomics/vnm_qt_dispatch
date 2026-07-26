#pragma once

#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QtGlobal>

#include <concepts>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

/**
 * @file vnm_qt_dispatch.h
 * @brief Qt Core-only safe dispatch primitives.
 *
 * This header owns the common QObject dispatch contract:
 *
 * - `post_with_exception_reporter()` always queues exact-void work and reports
 *   admission through `Post_result`. It contains queued target and reporter
 *   exceptions.
 * - `call()` invokes work synchronously on the receiver's affinity thread,
 *   propagates target exceptions, and reports dispatch failures through
 *   `Dispatch_error`.
 *
 * No `Q_ARG` or manual `QMetaType` registration is required. Cross-thread
 * arguments and functors are captured into queued lambdas; do not pass
 * non-owning views such as `QStringView` or `std::string_view` unless the
 * referenced storage is guaranteed to outlive execution on the receiver thread.
 *
 * `post()` reports queued target exceptions through Qt's warning channel.
 * `post_with_exception_reporter()` lets callers provide an explicit policy;
 * passing `nullptr` deliberately suppresses reporting.
 *
 * Task captures, bound member arguments, intermediate and result values, and
 * transported exception objects must have non-throwing, thread-agnostic
 * destruction. They can be destroyed on the submitting thread, the receiver
 * thread, or a thread removing posted events. Raw receiver pointers must remain
 * alive with stable affinity for the lifetime required by each operation.
 *
 * Qt signal members are protected. Code outside the emitter's class hierarchy
 * must use a public wrapper method when it cannot name the signal member
 * directly.
 */

namespace vnm {
namespace qt {

enum class Post_result
{
    QUEUED,
    RECEIVER_NULL,
    NO_THREAD_AFFINITY,
    SUBMISSION_FAILED,
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

using Exception_reporter = void (*)(std::exception_ptr);

namespace detail {

inline void report_dispatch_exception(std::exception_ptr exception) noexcept
{
    try {
        try {
            std::rethrow_exception(exception);
        }
        catch (const std::exception& e) {
            qWarning(
                "vnm::qt::post: Queued target threw an exception: %s",
                e.what());
        }
        catch (...) {
            qWarning("vnm::qt::post: Queued target threw an unknown exception.");
        }
    }
    catch (...) {
        // A diagnostic failure must not escape the Qt callback.
    }
}

template<class TaskSource>
using stored_task_t = std::decay_t<TaskSource>;

template<class TaskSource>
concept Storable_task =
    std::constructible_from<stored_task_t<TaskSource>, TaskSource&&> &&
    std::move_constructible<stored_task_t<TaskSource>>;

template<class TaskSource>
concept Invocable_stored_task =
    Storable_task<TaskSource> &&
    std::invocable<stored_task_t<TaskSource>&>;

template<class TaskSource>
concept Exact_void_task =
    Invocable_stored_task<TaskSource> &&
    std::same_as<std::invoke_result_t<stored_task_t<TaskSource>&>, void>;

template<class TaskSource>
concept Callable_call_task =
    Invocable_stored_task<TaskSource> &&
    !std::is_reference_v<std::invoke_result_t<stored_task_t<TaskSource>&>> &&
    (std::is_void_v<std::invoke_result_t<stored_task_t<TaskSource>&>> ||
     std::move_constructible<std::invoke_result_t<stored_task_t<TaskSource>&>>);

template<class Obj, class Method, class... Args>
concept Storable_member_call =
    std::same_as<Obj, std::remove_cv_t<Obj>> &&
    std::derived_from<Obj, QObject> &&
    std::is_member_function_pointer_v<Method> &&
    (std::constructible_from<std::decay_t<Args>, Args&&> && ...) &&
    (std::move_constructible<std::decay_t<Args>> && ...) &&
    std::invocable<Method, Obj*, std::decay_t<Args>&&...>;

template<class Obj, class Method, class... Args>
concept Exact_void_member_call =
    Storable_member_call<Obj, Method, Args...> &&
    std::same_as<
        std::invoke_result_t<Method, Obj*, std::decay_t<Args>&&...>,
        void>;

template<class Obj, class Method, class... Args>
concept Callable_member_call =
    Storable_member_call<Obj, Method, Args...> &&
    !std::is_reference_v<
        std::invoke_result_t<Method, Obj*, std::decay_t<Args>&&...>> &&
    (std::is_void_v<
         std::invoke_result_t<Method, Obj*, std::decay_t<Args>&&...>> ||
     std::move_constructible<
         std::invoke_result_t<Method, Obj*, std::decay_t<Args>&&...>>);

template<class Obj, class Method, class... Args>
auto bind_member_once(Obj* object, Method method, Args&&... args)
{
    using stored_args_t = std::tuple<std::decay_t<Args>...>;

    return [object,
            method,
            values = stored_args_t(std::forward<Args>(args)...)]()
               mutable -> decltype(auto)
    {
        return std::apply(
            [object, method](auto&... values) -> decltype(auto)
            {
                return std::invoke(method, object, std::move(values)...);
            },
            values);
    };
}

[[nodiscard]] inline bool is_current_thread(const QThread* target_thread) noexcept
{
    if (target_thread == nullptr) {
        return false;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    return target_thread->isCurrentThread();
#else
    // Qt 6.5-6.7 expose no public non-adopting affinity predicate.
    return QThread::currentThread() == target_thread;
#endif
}

enum class Call_state
{
    PENDING,
    COMPLETED,
    THREW,
};

} // namespace detail

/**
 * @brief Always enqueue an exact-void task on a QObject's event queue.
 *
 * `QUEUED` reports Qt admission only, never execution. Receiver destruction,
 * explicit event removal, or event-loop shutdown can still cancel accepted
 * work, and this API does not report that later cancellation. The context must
 * remain alive with stable thread affinity from entry through return. The queued
 * task is stored once from the source and invoked as an lvalue. Target
 * exceptions are contained and passed to `reporter` when it is non-null; reporter
 * exceptions are contained as well. Null receivers, missing affinity, storage
 * failure, and Qt rejection return the corresponding non-`QUEUED` result.
 *
 * Task captures, bound member arguments, intermediate and result values,
 * transported exception objects, and any other queued captures must have
 * non-throwing, thread-agnostic destruction and must not depend on a particular
 * thread's TLS. They can be destroyed on the submitting thread, the receiver
 * thread, or a thread removing posted events. `std::exception_ptr` is the
 * intentional cross-thread exception transport; it carries shared ownership of
 * the exception object, not a TLS address or current-thread identity. This
 * raw-pointer contract does not pin receiver lifetime, and no `QPointer`
 * observation can make concurrent submission and destruction safe.
 */
template<class TaskSource>
requires detail::Exact_void_task<TaskSource>
[[nodiscard(
    "post_with_exception_reporter() reports admission only; queued work can still be cancelled")]]
Post_result post_with_exception_reporter(
    QObject* context,
    TaskSource&& task_source,
    Exception_reporter reporter) noexcept
{
    if (context == nullptr) {
        return Post_result::RECEIVER_NULL;
    }
    if (context->thread() == nullptr) {
        return Post_result::NO_THREAD_AFFINITY;
    }

    try {
        using task_t = detail::stored_task_t<TaskSource>;
        task_t task(std::forward<TaskSource>(task_source));

        const bool accepted = QMetaObject::invokeMethod(
            context,
            [task = std::move(task), reporter]() mutable noexcept
            {
                try {
                    std::invoke(task);
                }
                catch (...) {
                    const std::exception_ptr target_exception =
                        std::current_exception();
                    if (reporter != nullptr) {
                        try {
                            reporter(target_exception);
                        }
                        catch (...) {
                            // Reporter failure must not escape the Qt callback.
                        }
                    }
                }
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

/**
 * @brief Always enqueue an exact-void task with Qt warning reporting.
 *
 * This overload has the full `post_with_exception_reporter()` admission,
 * lifetime, cancellation, and destruction contract. Target and reporter
 * exceptions never escape the queued Qt callback.
 */
template<class TaskSource>
requires detail::Exact_void_task<TaskSource>
[[nodiscard("vnm::qt::post() reports admission only; queued work can still be cancelled")]]
Post_result post(QObject* context, TaskSource&& task_source) noexcept
{
    return post_with_exception_reporter(
        context,
        std::forward<TaskSource>(task_source),
        detail::report_dispatch_exception);
}

/**
 * @brief Invoke a stored task synchronously on a QObject's affinity thread.
 *
 * The task is stored once before choosing the direct or cross-thread path and
 * is invoked as an lvalue in both paths. The original target exception is
 * propagated. Dispatch failures throw `Dispatch_error`; non-void results are
 * moved to the caller, and reference results are rejected by the constraints.
 *
 * The context must remain alive with stable affinity throughout the call. A
 * cross-thread submission can be rejected or can be cancelled after admission
 * but before execution. Its owner must keep the receiver event loop servicing
 * events until execution or cancellation. This operation is intentionally
 * unbounded; callers must not use it during shutdown unless the owner provides
 * that progress guarantee. Rejection and cancellation throw `Dispatch_error`
 * with `SUBMISSION_FAILED` and `CANCELLED_BEFORE_EXECUTION`, respectively.
 *
 * Task captures, bound member arguments, intermediate and result values,
 * transported exception objects, and any other queued captures must have
 * non-throwing, thread-agnostic destruction and must not depend on a particular
 * thread's TLS. They can be destroyed on the calling thread, the receiver
 * thread, or a thread removing posted events. `std::exception_ptr` is the
 * intentional cross-thread exception transport; it carries shared ownership of
 * the exception object, not a TLS address or current-thread identity.
 */
template<class TaskSource>
requires detail::Callable_call_task<TaskSource>
[[nodiscard]] auto call(QObject* context, TaskSource&& task_source)
    -> std::invoke_result_t<detail::stored_task_t<TaskSource>&>
{
    using task_t   = detail::stored_task_t<TaskSource>;
    using result_t = std::invoke_result_t<task_t&>;

    if (context == nullptr) {
        throw Dispatch_error(
            Dispatch_errc::RECEIVER_NULL,
            "Qt dispatch receiver is null.");
    }
    if (context->thread() == nullptr) {
        throw Dispatch_error(
            Dispatch_errc::NO_THREAD_AFFINITY,
            "Qt dispatch receiver has no thread affinity.");
    }

    std::optional<task_t> task;
    try {
        task.emplace(std::forward<TaskSource>(task_source));
    }
    catch (...) {
        throw Dispatch_error(
            Dispatch_errc::SUBMISSION_FAILED,
            "Qt dispatch task storage failed.");
    }

    if (detail::is_current_thread(context->thread())) {
        if constexpr (std::is_void_v<result_t>) {
            std::invoke(*task);
            return;
        }
        else {
            return std::invoke(*task);
        }
    }

    detail::Call_state call_state = detail::Call_state::PENDING;
    std::exception_ptr target_exception;
    [[maybe_unused]] std::conditional_t<
        std::is_void_v<result_t>,
        char,
        std::optional<result_t>> result{};

    bool accepted = false;
    try {
        accepted = QMetaObject::invokeMethod(
            context,
            [task = std::move(*task),
             &call_state,
             &target_exception,
             &result]() mutable noexcept
            {
                try {
                    if constexpr (std::is_void_v<result_t>) {
                        std::invoke(task);
                    }
                    else {
                        result.emplace(std::invoke(task));
                    }
                    call_state = detail::Call_state::COMPLETED;
                }
                catch (...) {
                    target_exception = std::current_exception();
                    call_state = detail::Call_state::THREW;
                }
            },
            Qt::BlockingQueuedConnection);
    }
    catch (...) {
        throw Dispatch_error(
            Dispatch_errc::SUBMISSION_FAILED,
            "Qt dispatch submission failed.");
    }

    if (!accepted) {
        throw Dispatch_error(
            Dispatch_errc::SUBMISSION_FAILED,
            "Qt rejected the blocking dispatch.");
    }
    if (call_state == detail::Call_state::PENDING) {
        throw Dispatch_error(
            Dispatch_errc::CANCELLED_BEFORE_EXECUTION,
            "Qt dispatch was cancelled before execution.");
    }
    if (call_state == detail::Call_state::THREW) {
        if (!target_exception) {
            throw Dispatch_error(
                Dispatch_errc::SUBMISSION_FAILED,
                "Qt dispatch failed without an exception.");
        }
        std::rethrow_exception(target_exception);
    }

    if constexpr (!std::is_void_v<result_t>) {
        if (!result) {
            throw Dispatch_error(
                Dispatch_errc::SUBMISSION_FAILED,
                "Qt dispatch completed without a result.");
        }
        return std::move(*result);
    }
}

namespace detail {

template<class Obj, class Method, class... Args>
requires Exact_void_member_call<Obj, Method, Args...>
[[nodiscard]] Post_result post_member_with_exception_reporter(
    Obj* object,
    Method method,
    Exception_reporter reporter,
    Args&&... args) noexcept
{
    if (object == nullptr) {
        return Post_result::RECEIVER_NULL;
    }
    if (object->thread() == nullptr) {
        return Post_result::NO_THREAD_AFFINITY;
    }

    try {
        auto task = bind_member_once(
            object,
            method,
            std::forward<Args>(args)...);
        return vnm::qt::post_with_exception_reporter(
            static_cast<QObject*>(object),
            std::move(task),
            reporter);
    }
    catch (...) {
        return Post_result::SUBMISSION_FAILED;
    }
}

} // namespace detail

/**
 * @brief Enqueue a typed exact-void member call using owned arguments.
 *
 * Ordinary arguments are decayed, owned, and consumed once. Use `std::ref()`
 * explicitly for a referenced argument; its referent must outlive execution or
 * cancellation, and cross-thread access remains the caller's responsibility.
 * `QUEUED` reports Qt admission only, never execution. Receiver destruction,
 * explicit event removal, or event-loop shutdown can still cancel accepted
 * work, and this API does not report that later cancellation. The receiver must
 * remain alive with stable thread affinity from entry through return. Null
 * receivers, missing affinity, argument storage failure, and Qt rejection return
 * the corresponding non-`QUEUED` result. Target exceptions are contained and
 * passed to the explicit reporter when it is non-null; reporter exceptions are
 * contained as well.
 *
 * Task captures, bound member arguments, intermediate and result values,
 * transported exception objects, and any other queued captures must have
 * non-throwing, thread-agnostic destruction and must not depend on a particular
 * thread's TLS. They can be destroyed on the submitting thread, the receiver
 * thread, or a thread removing posted events. `std::exception_ptr` is the
 * intentional cross-thread exception transport; it carries shared ownership of
 * the exception object, not a TLS address or current-thread identity. This
 * raw-pointer contract does not pin receiver lifetime, so concurrent submission
 * and destruction are unsafe.
 */
template<class Obj, class Method, class... Args>
requires detail::Exact_void_member_call<Obj, Method, Args...>
[[nodiscard(
    "post_with_exception_reporter() reports admission only; queued work can still be cancelled")]]
Post_result post_with_exception_reporter(
    Obj* object,
    Method method,
    Exception_reporter reporter,
    Args&&... args) noexcept
{
    return detail::post_member_with_exception_reporter(
        object,
        method,
        reporter,
        std::forward<Args>(args)...);
}

/**
 * @brief Enqueue a typed exact-void member call with Qt warning reporting.
 *
 * This overload has the full `post_with_exception_reporter()` admission,
 * lifetime, cancellation, argument-ownership, and destruction contract.
 * Target and reporter exceptions never escape the queued Qt callback.
 */
template<class Obj, class Method, class... Args>
requires detail::Exact_void_member_call<Obj, Method, Args...>
[[nodiscard("vnm::qt::post() reports admission only; queued work can still be cancelled")]]
Post_result post(Obj* object, Method method, Args&&... args) noexcept
{
    return post_with_exception_reporter(
        object,
        method,
        detail::report_dispatch_exception,
        std::forward<Args>(args)...);
}

/**
 * @brief Invoke a typed member call synchronously using owned arguments.
 *
 * Ordinary arguments are decayed, owned, and consumed once. Use `std::ref()`
 * for deliberate reference semantics; the referent must outlive this call and
 * any cross-thread access must be synchronized by the caller. The receiver must
 * remain alive with stable affinity throughout the call. A cross-thread
 * submission can be rejected or can be cancelled after admission but before
 * execution, and the owner must keep the receiver event loop servicing events
 * until execution or cancellation. The wait is intentionally unbounded.
 * Rejection and cancellation throw `Dispatch_error` with `SUBMISSION_FAILED`
 * and `CANCELLED_BEFORE_EXECUTION`, respectively.
 *
 * Task captures, bound member arguments, intermediate and result values,
 * transported exception objects, and any other queued captures must have
 * non-throwing, thread-agnostic destruction and must not depend on a particular
 * thread's TLS. They can be destroyed on the calling thread, the receiver
 * thread, or a thread removing posted events. `std::exception_ptr` is the
 * intentional cross-thread exception transport; it carries shared ownership of
 * the exception object, not a TLS address or current-thread identity.
 */
template<class Obj, class Method, class... Args>
requires detail::Callable_member_call<Obj, Method, Args...>
[[nodiscard]] decltype(auto) call(
    Obj* object,
    Method method,
    Args&&... args)
{
    if (object == nullptr) {
        throw Dispatch_error(
            Dispatch_errc::RECEIVER_NULL,
            "Qt dispatch receiver is null.");
    }
    if (object->thread() == nullptr) {
        throw Dispatch_error(
            Dispatch_errc::NO_THREAD_AFFINITY,
            "Qt dispatch receiver has no thread affinity.");
    }

    using task_t = decltype(detail::bind_member_once(
        object,
        method,
        std::forward<Args>(args)...));

    std::optional<task_t> task;
    try {
        task.emplace(detail::bind_member_once(
            object,
            method,
            std::forward<Args>(args)...));
    }
    catch (...) {
        throw Dispatch_error(
            Dispatch_errc::SUBMISSION_FAILED,
            "Qt dispatch member task storage failed.");
    }

    return vnm::qt::call(
        static_cast<QObject*>(object),
        std::move(*task));
}

} // namespace qt
} // namespace vnm
