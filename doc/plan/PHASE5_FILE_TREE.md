# Phase 5: File Tree Comparison

## Before Phase 5 (Legacy I/O)

```
src/
├── I/O Pollers (Legacy - DELETED)
│   ├── epoll.cpp/hpp          ❌ Linux epoll
│   ├── kqueue.cpp/hpp         ❌ BSD kqueue
│   ├── poll.cpp/hpp           ❌ POSIX poll
│   ├── select.cpp/hpp         ❌ POSIX select
│   ├── devpoll.cpp/hpp        ❌ Solaris /dev/poll
│   └── pollset.cpp/hpp        ❌ AIX pollset
│
├── TCP Transport (Legacy - DELETED)
│   ├── tcp_listener.cpp/hpp   ❌ Legacy TCP listener
│   └── tcp_connecter.cpp/hpp  ❌ Legacy TCP connecter
│
├── TCP Transport (ASIO - KEPT)
│   ├── asio/asio_poller.cpp/hpp          ✅ ASIO I/O poller
│   ├── asio/asio_tcp_listener.cpp/hpp    ✅ ASIO TCP listener
│   └── asio/asio_tcp_connecter.cpp/hpp   ✅ ASIO TCP connecter
│
├── IPC Transport (Legacy - KEPT for now)
│   ├── ipc_listener.cpp/hpp              ⚠️  Awaiting ASIO version
│   └── ipc_connecter.cpp/hpp             ⚠️  Awaiting ASIO version
│
└── Stream Base (Legacy - KEPT for IPC)
    ├── stream_engine_base.cpp/hpp        ⚠️  Used by IPC
    ├── stream_listener_base.cpp/hpp      ⚠️  Used by IPC
    ├── stream_connecter_base.cpp/hpp     ⚠️  Used by IPC
    └── zmtp_engine.cpp/hpp               ⚠️  Used by IPC
```

## After Phase 5 (ASIO-Only for TCP)

```
src/
├── I/O Poller (ASIO Only)
│   └── asio/asio_poller.cpp/hpp          ✅ Single I/O backend
│
├── TCP Transport (ASIO Only)
│   ├── asio/asio_tcp_listener.cpp/hpp    ✅ Pure ASIO
│   ├── asio/asio_tcp_connecter.cpp/hpp   ✅ Pure ASIO
│   └── asio/asio_zmtp_engine.cpp/hpp     ✅ Pure ASIO
│
├── IPC Transport (Legacy - Temporary)
│   ├── ipc_listener.cpp/hpp              ⚠️  Phase 6 target
│   └── ipc_connecter.cpp/hpp             ⚠️  Phase 6 target
│
└── Stream Base (Legacy - Temporary)
    ├── stream_engine_base.cpp/hpp        ⚠️  Phase 7 target
    ├── stream_listener_base.cpp/hpp      ⚠️  Phase 7 target
    ├── stream_connecter_base.cpp/hpp     ⚠️  Phase 7 target
    └── zmtp_engine.cpp/hpp               ⚠️  Phase 7 target
```

## Code Reduction Statistics

| Category | Before | After | Removed | Status |
|----------|--------|-------|---------|--------|
| I/O Pollers | 6 implementations | 1 (ASIO) | 5 pollers | ✅ Complete |
| TCP Transport | 2 implementations | 1 (ASIO) | 1 legacy | ✅ Complete |
| Total Files | 36 core I/O files | 20 core I/O files | 16 files | ✅ Complete |
| Lines of Code | ~12,834 lines | ~10,000 lines | ~2,834 lines | ✅ Complete |

## Conditional Compilation Cleanup

### Before (Complex)
```cpp
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO
    asio_tcp_listener_t *listener = new asio_tcp_listener_t(...);
#else
    tcp_listener_t *listener = new tcp_listener_t(...);
#endif
```

### After (Simple)
```cpp
asio_tcp_listener_t *listener = new asio_tcp_listener_t(...);
```

## Build System Simplification

### CMakeLists.txt Changes
- **Removed**: 12 poller source files
- **Removed**: 4 TCP legacy source files
- **Kept**: 12 IPC/stream base files (temporary)
- **Result**: ~30% fewer source files in build

### poller.hpp Changes
```cpp
// Before: 30+ lines of conditional includes
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO
#include "asio/asio_poller.hpp"
#elif defined ZMQ_IOTHREAD_POLLER_USE_KQUEUE
#include "kqueue.hpp"
#elif defined ZMQ_IOTHREAD_POLLER_USE_EPOLL
#include "epoll.hpp"
// ... 6+ more conditions

// After: 3 lines
#if !defined ZMQ_IOTHREAD_POLLER_USE_ASIO
#error Only ASIO poller is supported
#endif
#include "asio/asio_poller.hpp"
```

## Legend
- ✅ **Complete**: Fully migrated to ASIO
- ⚠️ **Temporary**: Awaiting future ASIO migration
- ❌ **Deleted**: Removed in Phase 5
