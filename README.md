# vnm_qt_dispatch

`vnm_qt_dispatch` provides C++20 helpers for dispatching typed callables through
Qt object affinity. The public target is
`vnm_qt_dispatch::vnm_qt_dispatch`; its only product dependency is `Qt6::Core`.

The API remains in `vnm::qt`:

- `post()` always requests queued delivery and reports admission through
  `Post_result`. Queued target exceptions are contained and reported through
  Qt's warning channel.
- `post_with_exception_reporter()` provides the same queued operation with an
  explicit exception reporter. A null reporter deliberately suppresses queued
  target-exception diagnostics.
- `call()` executes synchronously on the receiver's affinity thread, returns
  non-reference results, propagates target exceptions, and reports dispatch
  failures through `Dispatch_error`.

Callable and typed member-function overloads are available for all three
operations. Include the API with:

```cpp
#include <vnm_qt_dispatch/vnm_qt_dispatch.h>
```

## Admission and lifetime contract

`Post_result::QUEUED` means Qt accepted the event. It does not mean the work
executed. Receiver destruction, explicit event removal, or event-loop shutdown
can still cancel accepted work.

The receiver must remain alive with stable thread affinity for the duration
required by the operation. The raw receiver pointer does not pin lifetime.
Concurrent submission and destruction are unsafe.

Queued tasks and typed member arguments are stored by value and consumed once.
Use `std::ref()` only for deliberate reference semantics whose lifetime and
cross-thread synchronization are guaranteed by the caller. Task captures,
arguments, results, and transported exception objects must have non-throwing,
thread-agnostic destruction because Qt may destroy them on the submitting
thread, receiver thread, or a thread removing posted events.

Cross-thread `call()` is intentionally unbounded. The receiver owner must keep
its event loop servicing events until the operation executes or is cancelled.
Do not use it during shutdown without that progress guarantee.

## Installed package

```cmake
find_package(vnm_qt_dispatch 1 CONFIG REQUIRED)

target_link_libraries(my_target
    PRIVATE
        vnm_qt_dispatch::vnm_qt_dispatch
)
```

The installed version file uses same-major compatibility.

## Source dependency

Owned source consumers may provide a local checkout or use `FetchContent` with
the canonical `master` branch:

```cmake
include(FetchContent)

FetchContent_Declare(vnm_qt_dispatch
    GIT_REPOSITORY https://github.com/Varinomics/vnm_qt_dispatch.git
    GIT_TAG master
)
FetchContent_MakeAvailable(vnm_qt_dispatch)
```

FetchContent working and binary directories remain under the consumer's
`CMAKE_BINARY_DIR/_deps` unless that consumer deliberately supplies another
supported local source checkout.

## Building and testing

```text
cmake -S . -B build -DVNM_QT_DISPATCH_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The focused suite exercises queued and synchronous dispatch, typed member
calls, admission failures, exception transport and reporting, TLS teardown,
source-subproject consumption, installed-package consumption, version
compatibility, and `QT_NO_KEYWORDS`.

## License

`vnm_qt_dispatch` is distributed under the BSD 2-Clause License.
