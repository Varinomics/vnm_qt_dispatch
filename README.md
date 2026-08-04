# vnm_qt_dispatch

`vnm_qt_dispatch` is a single-header C++20 utility for dispatching owned work
through `QObject` thread affinity. Its only product dependency is Qt Core, and
its public CMake target is `vnm_qt_dispatch::vnm_qt_dispatch`.

Qt already provides the thread-safe functor overloads of
`QMetaObject::invokeMethod()`. This library adds a deliberately small policy
layer:

- `post()` always queues exact-`void` work and contains exceptions;
- `blocking_call()` executes on the receiver's affinity thread, returns a
  result, and propagates the original exception;
- callables and ordinary typed-member arguments are owned values;
- same-thread blocking calls execute inline rather than using a blocking queued
  connection;
- removal of an unexecuted blocking-call event is reported as typed
  cancellation.

```cpp
#include <vnm_qt_dispatch/vnm_qt_dispatch.h>

const auto admitted = vnm::qt::post(
    receiver,
    [value = std::move(value)]() mutable {
        consume(std::move(value));
    });

const int answer = vnm::qt::blocking_call(
    receiver,
    &Worker::compute,
    input);
```

## API

### Queued work

```cpp
vnm::qt::Post_result post(QObject* receiver, Task&& task) noexcept;

vnm::qt::Post_result post_with_exception_reporter(
    QObject* receiver,
    Task&& task,
    Reporter&& reporter) noexcept;
```

Both functions always request `Qt::QueuedConnection` and accept only work whose
stored callable returns exactly `void`. `post()` reports a thrown target
exception through `qWarning()`. The reporter overload accepts any owned callable
invocable with `std::exception_ptr`; reporter exceptions are contained too.
Use `vnm::qt::ignore_exceptions` when discarding target exceptions is deliberate.
`nullptr` and null function pointers remain supported.

`Post_result::QUEUED` means only that Qt accepted the queued event. It does not
mean that the task ran.

| Result | Meaning |
| --- | --- |
| `QUEUED` | Qt accepted the event. |
| `RECEIVER_NULL` | The receiver pointer was null. |
| `NO_THREAD_AFFINITY` | `receiver->thread()` was null. |
| `STORAGE_FAILED` | Owning the task/reporter or allocating its state failed. |
| `SUBMISSION_FAILED` | Receiver inspection or Qt event submission failed. |

The post APIs are `noexcept`; immediate failures are represented by this enum.
Accepted work can still be cancelled later by receiver destruction or explicit
event removal.

### Synchronous work

```cpp
decltype(auto) blocking_call(QObject* receiver, Task&& task);

decltype(auto) blocking_call_with_submission_observer(
    QObject* receiver,
    Task&& task,
    SubmissionObserver&& submission_observer);
```

If the receiver belongs to the calling thread, the stored task executes inline.
Otherwise the task is queued and the caller waits without a timeout.
Non-reference results, including move-only results, are supported. Task setup,
affinity inspection, target execution, result storage, result transfer, and
exceptions thrown by Qt while preparing submission retain their original types.
A `false` return from Qt is mapped to `Dispatch_errc::SUBMISSION_FAILED`.

`blocking_call_with_submission_observer()` exposes the exact point when the
call can no longer fail before dispatch. Its noexcept observer runs on the
calling thread immediately before same-thread execution, or after Qt accepts a
cross-thread event and before the caller waits. Setup and submission failures do
not invoke it. This lets lifecycle owners close admission, wait until every
admitted caller has either submitted or withdrawn, and cancel accepted events
without polling the receiver queue.

Dispatch-state failures throw `vnm::qt::Dispatch_error` with one of:

- `Dispatch_errc::RECEIVER_NULL`;
- `Dispatch_errc::NO_THREAD_AFFINITY`;
- `Dispatch_errc::SUBMISSION_FAILED`;
- `Dispatch_errc::CANCELLED_BEFORE_EXECUTION`.

The cross-thread implementation deliberately does **not** use
`Qt::BlockingQueuedConnection`. The queued functor owns a `std::promise`; the
caller waits on its future. Execution fulfils the promise after recording the
result or exception. Destruction of an unexecuted queued functor abandons the
promise, and `broken_promise` is translated to
`CANCELLED_BEFORE_EXECUTION`. This avoids relying on Qt's private
blocking-event semaphore/latch implementation.

The wait is intentionally unbounded. The receiver thread must continue
processing events until the callback executes or the queued event is destroyed.
Holding a lock needed by the receiver, cyclic blocking calls, a stopped event
loop, or incorrect shutdown ordering can still deadlock.

## Typed member calls

All operations have typed member-function overloads:

```cpp
post(worker, &Worker::consume, std::move(value));
blocking_call(worker, &Worker::compute, input);
```

Ordinary arguments are decayed, owned, and consumed once. This makes a method
that takes a value, `const T&`, or `T&&` safe and predictable. Use `std::ref()`
only when reference semantics are deliberate and the referenced object is
sufficiently long-lived and synchronized.

Decay does not turn a pointer or view into owning storage. A stored
`std::string_view`, `QStringView`, span, iterator, or raw pointer still requires
its referent to outlive execution.

## Receiver and destruction contract

The raw receiver pointer is not pinned. It must be valid, and its affinity must
not change concurrently, while the library reads `thread()` and submits the
operation. A `QPointer` cannot make this check-then-submit interval safe against
concurrent destruction.

After successful submission, normal `QObject` destruction removes pending
posted events. A post is then silently cancelled; a waiting `blocking_call()`
receives `CANCELLED_BEFORE_EXECUTION`.

Stored tasks, reporters, arguments, results, and transported exception objects
can be destroyed on the submitting thread, receiver thread, or a thread that
removes posted events. Their destructors must therefore be non-throwing and
thread-agnostic. The C++ constraints enforce non-throwing destruction where the
type system can express it.

## CMake consumption

Installed package:

```cmake
find_package(vnm_qt_dispatch 2 CONFIG REQUIRED)

target_link_libraries(my_target
    PRIVATE
        vnm_qt_dispatch::vnm_qt_dispatch
)
```

Owned source dependency:

```cmake
include(FetchContent)

FetchContent_Declare(vnm_qt_dispatch
    GIT_REPOSITORY https://github.com/Varinomics/vnm_qt_dispatch.git
    GIT_TAG master
)
FetchContent_MakeAvailable(vnm_qt_dispatch)
```

## Building and testing

```text
cmake -S . -B build -DVNM_QT_DISPATCH_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The library requires C++ exception support; builds with `QT_NO_EXCEPTIONS` are
rejected at compile time.

The suite covers direct and queued dispatch, owned member arguments, move-only
and explicitly movable types, admission failures, exception transport,
reporter containment, event-removal cancellation, destruction ordering,
foreign `std::thread` teardown, source and installed consumption, and
`QT_NO_KEYWORDS`.

CI should exercise every supported Qt line because cancellation depends on Qt
releasing an event-owned functor when a pending meta-call event is removed. The
implementation no longer depends on the private mechanics used by
`Qt::BlockingQueuedConnection`.

## Version 2 migration

Version 2 intentionally changes the source API:

- `call(...)` is now `blocking_call(...)`;
- `Post_result::QUEUED` continues to mean admission, not execution;
- preparation failure is distinguished as `Post_result::STORAGE_FAILED`;
- exception reporters may be arbitrary owned callables rather than only
  function pointers.

## License

`vnm_qt_dispatch` is distributed under the BSD 2-Clause License.
