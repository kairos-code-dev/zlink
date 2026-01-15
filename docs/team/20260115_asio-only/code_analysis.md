# ASIO-Only Migration - Code Analysis

**Date:** 2026-01-15
**Author:** dev-cxx
**Phase:** 0 (Baseline)

## Executive Summary

This document analyzes the current state of `ZMQ_IOTHREAD_POLLER_USE_ASIO` macro usage in the zlink codebase. The analysis identifies 47 conditional compilation blocks across 41 files, categorized by pattern type for systematic removal during Phase 1-2.

## Macro Usage Statistics

### Overall Summary

| Category | Count |
|----------|-------|
| Total files with macro | 41 |
| Total conditional blocks (`#if`) | 47 |
| Total macro occurrences | 85 |

### Pattern Classification

| Pattern Type | Count | Removal Strategy |
|--------------|-------|------------------|
| Pattern 1: `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` (standalone) | 18 | Direct removal |
| Pattern 2: `#if ... && ZMQ_HAVE_ASIO_SSL` | 10 | Keep feature macro only |
| Pattern 3: `#if ... && ZMQ_HAVE_WS` | 8 | Keep feature macro only |
| Pattern 4: `#if ... && ZMQ_HAVE_IPC` | 6 | Keep feature macro only |
| Pattern 5: `#if ... && ZMQ_HAVE_ASIO_WS` | 4 | Keep feature macro only |
| Pattern 6: Error guard `#if !defined` | 1 | Remove entirely |

## Core Files Analysis

Priority files for Phase 1 (Transport Layer):

| File | Macro Count | Pattern Types |
|------|-------------|---------------|
| `src/session_base.cpp` | 3 | Patterns 1, 2, 3 |
| `src/socket_base.cpp` | 3 | Patterns 1, 2, 3 |
| `src/io_thread.hpp` | 2 | Pattern 1 |
| `src/io_thread.cpp` | 1 | Pattern 1 |
| `src/poller.hpp` | 2 | Patterns 1, 6 |

### session_base.cpp Analysis

```cpp
// Line 12: Include guard - Pattern 1 (REMOVE entirely)
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO
#include "asio/asio_tcp_connecter.hpp"
...
#endif

// Line 548: Feature combination - Pattern 2 (KEEP ZMQ_HAVE_ASIO_SSL only)
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL
#include "asio/asio_tls_connecter.hpp"
...
#endif

// Line 564: Feature combination - Pattern 3 (KEEP ZMQ_HAVE_WS only)
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS
#include "asio/asio_ws_connecter.hpp"
...
#endif
```

### socket_base.cpp Analysis

```cpp
// Line 26: Include guard - Pattern 1 (REMOVE entirely)
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO
...
#endif

// Line 478: Feature combination - Pattern 2 (KEEP ZMQ_HAVE_ASIO_SSL only)
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL
...
#endif

// Line 529: Feature combination - Pattern 3 (KEEP ZMQ_HAVE_WS only)
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS
...
#endif
```

### poller.hpp Analysis

```cpp
// Line 7-9: Error guard - Pattern 6 (REMOVE entirely after ASIO-only)
#if !defined ZMQ_IOTHREAD_POLLER_USE_ASIO
#error ZMQ_IOTHREAD_POLLER_USE_ASIO must be defined - only ASIO poller is supported
#endif
```

This error guard should be removed LAST (Phase 3) after all other ASIO macro usages are cleaned up.

## ASIO Directory Files

All files in `src/asio/` directory use wrapper guards:

| File | Start Pattern | End Pattern |
|------|---------------|-------------|
| `asio_engine.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_engine.hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_poller.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_poller.hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `tcp_transport.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `tcp_transport.hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_tcp_listener.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_tcp_listener.hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_tcp_connecter.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_tcp_connecter.hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_zmtp_engine.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `asio_zmtp_engine.hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `i_asio_transport.hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` | `#endif // ZMQ_IOTHREAD_POLLER_USE_ASIO` |

### Feature-Dependent ASIO Files

| File | Guard Pattern |
|------|---------------|
| `ssl_transport.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL` |
| `ssl_context_helper.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL` |
| `asio_tls_listener.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL` |
| `asio_tls_connecter.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL` |
| `ipc_transport.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_IPC` |
| `asio_ipc_listener.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_IPC` |
| `asio_ipc_connecter.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_IPC` |
| `ws_transport.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS` |
| `wss_transport.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS && defined ZMQ_HAVE_ASIO_SSL` |
| `asio_ws_engine.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS` |
| `asio_ws_listener.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS` |
| `asio_ws_connecter.cpp/hpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_WS` |

## Removal Strategy

### Phase 1: Transport Layer (session_base.cpp, socket_base.cpp)

1. **Step 1:** Remove standalone ASIO guards
   - Change `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` to unconditional includes

2. **Step 2:** Simplify feature combinations
   - Change `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL` to `#if defined ZMQ_HAVE_ASIO_SSL`

### Phase 2: I/O Thread Layer (io_thread.hpp, io_thread.cpp)

1. Remove ASIO guards from io_thread files
2. Make ASIO headers unconditional includes

### Phase 3: Build System (poller.hpp, CMakeLists.txt, platform.hpp.in)

1. Remove error guard from poller.hpp
2. Keep `ZMQ_IOTHREAD_POLLER_USE_ASIO=1` in CMake for backward compatibility
3. Clean up platform.hpp.in

## Feature Macros to Preserve

These macros are independent of ASIO and MUST be preserved:

| Macro | Purpose |
|-------|---------|
| `ZMQ_HAVE_ASIO_SSL` | TLS transport availability |
| `ZMQ_HAVE_ASIO_WS` | WebSocket engine availability |
| `ZMQ_HAVE_ASIO_WSS` | Secure WebSocket availability |
| `ZMQ_HAVE_WS` | WebSocket protocol support |
| `ZMQ_HAVE_WSS` | Secure WebSocket protocol support |
| `ZMQ_HAVE_TLS` | TLS protocol support |
| `ZMQ_HAVE_IPC` | IPC protocol support (Unix only) |

## Test Files Analysis

Test files also use the macro for conditional test execution:

| File | Pattern |
|------|---------|
| `tests/test_asio_tcp.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `tests/test_asio_connect.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO` |
| `tests/test_asio_poller.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && !defined ZMQ_HAVE_WINDOWS` |
| `tests/test_asio_ssl.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_SSL` |
| `tests/test_asio_ws.cpp` | `#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO && defined ZMQ_HAVE_ASIO_WS` |

These will need to be updated after src/ cleanup is complete.

## Risk Assessment

### Low Risk (Pattern 1)
- Standalone ASIO guards can be removed safely
- No functional impact expected

### Medium Risk (Patterns 2-5)
- Feature combinations need careful review
- Must preserve feature detection logic

### High Risk (Pattern 6)
- Error guard removal should be last step
- Verify all ASIO code is unconditional first

## Recommended Order of Changes

1. **Phase 1-A:** `session_base.cpp` includes
2. **Phase 1-B:** `socket_base.cpp` includes
3. **Phase 2-A:** `io_thread.hpp` includes
4. **Phase 2-B:** `io_thread.cpp` logic
5. **Phase 2-C:** `src/asio/*.hpp` header guards
6. **Phase 2-D:** `src/asio/*.cpp` source guards
7. **Phase 3-A:** `poller.hpp` error guard
8. **Phase 3-B:** CMakeLists.txt cleanup
9. **Phase 3-C:** platform.hpp.in cleanup
10. **Phase 4:** Test file updates

## Conclusion

The codebase has 85 occurrences of `ZMQ_IOTHREAD_POLLER_USE_ASIO` across 41 files. The majority (18 standalone blocks) can be directly removed, while 28 feature-combination blocks need to be simplified by removing only the ASIO portion.

Estimated work:
- Phase 1: 2-3 days
- Phase 2: 2-3 days
- Phase 3: 1-2 days
- Total: 5-8 days

---

**Last Updated:** 2026-01-15
**Status:** Phase 0 Complete
