# Changelog

All notable changes to zlink will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

## [5.0.4] - 2026-03-16

### Added

**Registry-Backed Topology And Monitor Coverage**
- Added registry-backed gateway peer snapshot/query APIs and aligned topology snapshots with the current monitor lifecycle so SPOT and gateway services expose a more stable introspection surface.
- Added delivery-ready and pair monitor readiness coverage, plus deterministic monitor teardown paths for SPOT workflows that previously depended on timing-sensitive cleanup.

### Changed

**Service Lifecycle Simplification**
- Completed the SpotNode and Gateway control-plane refactor so public service operations follow tighter thread-safe socket contracts with fewer lifecycle edge cases exposed to callers.
- Unified the direct callback receive model across the runtime, tests, and perf suites to keep the public data path consistent with the callback-only contract.

### Fixed

**SPOT Runtime Stability**
- Restored multi-client benchmark and shutdown semantics, tightened WSS subscription readiness, and fixed delivery-ready synchronization gaps that could leave SPOT workloads in inconsistent startup or teardown states.
- Preserved raw monitor teardown tracking across timeout paths and removed stale API/cleanup behavior that could interfere with repeated service bring-up/tear-down cycles.

## [5.0.3] - 2026-03-12

### Fixed

**Release Build Parity**
- Aligned the core GitHub Actions native release workflow with the current core build baseline by building Linux release artifacts on Ubuntu 24.04 and using the default C++17 setting on Windows release jobs.
- This keeps the packaged native libraries used by bindings closer to the locally built core runtime when benchmarking STREAM raw/callback/LEN32BE paths.

## [5.0.2] - 2026-03-10

### Fixed

**Windows Core Build Portability**
- Guarded POSIX-only `unistd.h` includes and replaced direct `usleep()` calls with platform-specific 1ms sleep helpers in Discovery and SpotNode so Windows release builds complete.

## [5.0.1] - 2026-03-10

### Fixed

**macOS ARM64 Core Build**
- Replaced the non-portable `ECOMM` monitor-handshake failure path with a portable protocol error so Apple Silicon release builds complete successfully.

## [5.0.0] - 2026-03-10

### Breaking Changes

**Service Option And Handle API Refresh**
- Replaced service-specific `*_setsockopt` and unsafe raw-socket accessors on Discovery, Gateway, Receiver, and SpotNode with explicit option, routing-id, monitor, and poller-facing APIs.
- Discovery now connects through a Registry bootstrap endpoint instead of a direct PUB-only endpoint.
- SpotNode removes the direct registry-connect and internal PUB/SUB socket borrowing APIs in favor of managed facade operations.

**SPOT Data Plane Rewrite**
- Reworked SPOT around a proxy-based data plane and node-owned default pub/sub facades.
- Removed the previous direct-routing/async-mode surface in favor of thread-safe facade publishing and managed subscriber handling.

### Added

**Service Introspection And Monitoring**
- Added service monitor open/recv/close APIs for Discovery, Gateway, Receiver, SpotPub, and SpotSub.
- Added registry topology snapshot/query APIs and representative routing-id APIs across service facades.
- Added poller integration and peer/option surfaces for Gateway, Receiver, SpotPub, and SpotSub services.

**Spot Node Direct Facade**
- Added node-level publish, subscribe, unsubscribe, handler, and recv APIs backed by node-owned default SpotPub and SpotSub instances.
- Added per-node default pub/sub option application and direct facade helpers for SPOT applications.

### Fixed

**Transport And Stream Reliability**
- Preserved WebSocket and WSS stream message boundaries.
- Disabled speculative writes on WS transports until the handshake path is ready.
- Enforced stream ready ordering before payload delivery.

**Thread Safety And Runtime Stability**
- Made random initialization thread-safe.
- Restored single-throughput semantics and pub/sub flow control behavior in the perf/runtime path.

## [4.0.2] - 2026-03-07

### Fixed

**Inproc PUB/SUB Alternating Stability**
- Fixed a stale mailbox/HWM command-processing path in `PUB` send handling.
- Prevents intermittent hangs/timeouts in alternating single-threaded `inproc` PUB/SUB workloads.
- Added a dedicated regression test for repeated alternating send/recv on `inproc`.

**Gateway Router Peer Sampling**
- Fixed `zlink_gateway_router_peers()` so peer enumeration no longer forces the Gateway into pollable/raw mode.
- Prevents the `core/v4.0.1` regression where service-instance queue sampling could cause subsequent `gateway.send()` calls to fail.
- The current Gateway regression suite covers the service-instance router-peers path.

## [4.0.1] - 2026-03-07

### Removed

**Build System Cleanup**
- Removed Autotools build system (configure.ac, acinclude.m4, Makefile.am files)
- Removed GYP build configuration (core/builds/gyp/project.gyp)
- Removed MinGW/Cygwin Makefiles (Makefile.mingw32, Makefile.cygwin, README.cygwin.md)
- **CMake is now the only supported build system**

**Socket Statistics API**
- Removed `zlink_socket_stats` and `zlink_socket_stats_ex` APIs
- Removed `ZLINK_STATS_COUNTERS` and `ZLINK_STATS_TIMESTAMPS` socket options

**Rationale:**
- CMake already supports all target platforms (Windows, Linux, macOS)
- Legacy build files referenced removed source files (req.cpp, pgm_socket.cpp, etc.)
- Legacy build files supported removed features (CURVE, TIPC, PGM, NORM, VMCI, UDP)
- Reduces technical debt by ~3,800 lines
- Simplifies maintenance and onboarding

### Changed

**Context Defaults**
- Changed default IO thread count to 2 (`ZLINK_IO_THREADS_DFLT`).

**Build Scripts**
- Updated CI/CD scripts to use CMake instead of autotools:
  - core/ci_build.sh - Main CI build script
  - core/builds/ci/cmake/ci_build.sh - CMake-specific builds
  - core/builds/ci/valgrind/ci_build.sh - Memory testing

### Migration Guide

If you were using Autotools or other legacy build systems:

**Before:**
```bash
./core/autogen.sh
./configure
make
make install
```

**After:**
```bash
cmake -B build
cmake --build build
cmake --install build
```

All platforms (Windows, Linux, macOS) now use CMake exclusively. See core/builds/ directory for platform-specific build scripts.

## [0.3.0] - 2026-01-15

### Breaking Changes

**ASIO-Only Backend Migration**

The project has migrated to use ASIO as the only I/O backend. This is a **breaking change** that requires rebuilding any applications linked against this library.

- **Removed:** Conditional compilation for I/O backend selection
- **Change:** ASIO (Boost.Asio) is now the mandatory I/O backend
- **Impact:** Applications must be rebuilt; no source code changes required
- **ABI Compatibility:** ABI has changed; dynamic linking requires library update

### Changed

- **CMake option cleanup**:
  - `WITH_BOOST_ASIO` removed - ASIO backend is now mandatory
  - `WITH_ASIO_SSL` renamed to `WITH_TLS` - controls TLS/WSS transport support
  - `WITH_ASIO_WS` removed - WebSocket is now a core transport (always enabled)

- **Build directory naming**:
  - Benchmark default: `build-bench-asio` → `build/bench`
  - Documentation examples updated

- **Build system simplification**:
  - Removed I/O poller selection logic
  - Removed `ZLINK_IOTHREAD_POLLER_USE_ASIO` conditional compilation guards
  - Cleaned up 10 conditional compilation blocks across source files

### Performance

Benchmark results show all metrics within ±10% tolerance of baseline:

- PAIR TCP: -1.4% (within acceptable range)
- PUBSUB TCP: -3.4% (within acceptable range)
- DEALER/ROUTER TCP: +4.0% (improved)

### Migration Guide

For existing build scripts:
```bash
# Before:
cmake -B build -DWITH_BOOST_ASIO=ON -DWITH_ASIO_SSL=ON -DWITH_ASIO_WS=ON

# After:
cmake -B build -DWITH_TLS=ON
# ASIO and WebSocket are now always enabled
# TLS option controls both tls:// and wss:// transports
```

**Key changes:**
- ASIO backend: Mandatory (not optional)
- WebSocket (ws://): Mandatory (not optional)
- TLS (tls://) and WSS (wss://): Optional via WITH_TLS (default ON)

Internal C++ defines (`ZLINK_HAVE_ASIO_SSL`, `ZLINK_HAVE_ASIO_WS`) remain unchanged for compatibility.

### Technical Details

- Test coverage: 61/61 tests passing (100%)
- Supported platforms: Windows, Linux, macOS (x64 and ARM64)

## [0.2.0] - 2026-01-13

### Added
- **ASIO Backend**: Boost.Asio-based I/O using bundled Boost headers
  - True proactor pattern with `async_accept`, `async_connect`, `async_read`, `async_write`
  - Platform-specific optimizations: epoll (Linux), kqueue (macOS), IOCP (Windows)
- **TLS Transport**: Native TLS protocol (`tls://`) using OpenSSL
  - Socket options: `ZLINK_TLS_CERT`, `ZLINK_TLS_KEY`, `ZLINK_TLS_CA`, `ZLINK_TLS_HOSTNAME`
  - Server and client authentication
  - Mutual TLS support
- **WebSocket Support**: Standard WebSocket transport
  - `ws://` - Plain WebSocket
  - `wss://` - WebSocket over TLS
  - Uses Boost.Beast for WebSocket framing
- **TLS Documentation**: Comprehensive TLS usage guide at `doc/TLS_USAGE_GUIDE.md`
- **Version Tracking**: Added `ZLINK_VERSION` to VERSION file

### Changed
- **Default Build Configuration**: ASIO and TLS now enabled by default
  - ASIO backend is mandatory (no option needed)
  - WebSocket is always enabled (no option needed)
  - `WITH_TLS=ON` (default, replaces `WITH_ASIO_SSL`)
- **CMake Configuration**: Simplified build options focused on ASIO backend
- **Documentation**: Updated CLAUDE.md and README.md to reflect current architecture

### Removed
- **Socket Types**:
  - `ZLINK_STREAM`: Raw TCP stream socket (use WebSocket instead)
  - `ZLINK_REQ/REP`: Request-reply pattern (removed in v0.1.3)
  - `ZLINK_PUSH/PULL`: Pipeline pattern (removed in v0.1.3)

- **Protocols**:
  - `tipc://`: Transparent Inter-Process Communication
  - `vmci://`: VMware Virtual Machine Communication Interface
  - `pgm://`, `epgm://`: Pragmatic General Multicast
  - `norm://`: NACK-Oriented Reliable Multicast
  - `udp://`: Unicast and multicast UDP

- **Encryption**:
  - CURVE encryption (replaced by TLS)
  - libsodium dependency

- **Tests**:
  - TIPC protocol tests (3 tests removed)
  - Tests now total 64 (down from 67)

### Fixed
- **Build System**: Proper dependency management for OpenSSL
- **Test Suite**: Removed tests for unsupported protocols
- **Platform Support**: Improved Windows ARM64 build configuration

### Migration Notes
- **From CURVE to TLS**: Applications using CURVE must migrate to TLS transport
  - Replace `ZLINK_CURVE_*` options with `ZLINK_TLS_*` options
  - Update connection strings from `tcp://` to `tls://`
  - Use PEM-formatted certificates instead of binary keys
- **From STREAM sockets**: Migrate to WebSocket (`ws://`, `wss://`)
- **From removed protocols**: Migrate to `tcp://`, `ipc://`, or WebSocket

## [0.1.3]

### Removed
- **Socket Types**:
  - `ZLINK_REQ/REP`: Request-reply pattern
  - `ZLINK_PUSH/PULL`: Pipeline pattern
- **Monitoring**:
  - `ZLINK_EVENT_PIPES_STATS` event
  - `zlink_socket_monitor_pipes_stats()` function

## [0.1.2]

### Removed
- **Draft API**: Completely removed all draft socket types and options
  - Socket types: SERVER, CLIENT, RADIO, DISH, GATHER, SCATTER, DGRAM, PEER, CHANNEL
  - WebSocket transport (was draft feature, re-added in v0.2.0 as stable)
  - Draft socket options: `ZLINK_RECONNECT_STOP`, `ZLINK_ZAP_ENFORCE_DOMAIN`, etc.

## [0.1.1]

### Added
- Initial release based on libzlink 4.3.5
- Cross-platform build scripts for Linux, macOS, Windows
- Support for x64 and ARM64 architectures
- 67 tests from upstream libzlink test suite

### Features
- Full libzlink 4.3.5 API (except CURVE encryption)
- All standard socket types
- All standard protocols (tcp, ipc, inproc, tipc, vmci, pgm, norm, udp)
- CMake-based build system
- GitHub Actions CI/CD pipeline

---

## Supported Platforms

All versions support the following platforms:

| Platform | Architectures | Build System |
|----------|---------------|--------------|
| Linux | x64, ARM64 | CMake + GCC/Clang |
| macOS | x86_64, ARM64 | CMake + Clang |
| Windows | x64, ARM64 | CMake + MSVC |

## Build Requirements

### v0.2.0+
- CMake 3.10+
- C++11 compiler (GCC 5+, Clang 3.8+, MSVC 2015+)
- OpenSSL (for TLS support)
- Boost.Asio (bundled in `core/external/boost/`)

### v0.1.x
- CMake 3.10+
- C++11 compiler (GCC 5+, Clang 3.8+, MSVC 2015+)

## Protocol Support by Version

| Protocol | v0.1.1 | v0.1.2 | v0.1.3 | v0.2.0 |
|----------|--------|--------|--------|--------|
| tcp | ✓ | ✓ | ✓ | ✓ |
| ipc | ✓ | ✓ | ✓ | ✓ |
| inproc | ✓ | ✓ | ✓ | ✓ |
| ws | - | - | - | ✓ |
| wss | - | - | - | ✓ |
| tls | - | - | - | ✓ |
| tipc | ✓ | ✓ | ✓ | - |
| vmci | ✓ | ✓ | ✓ | - |
| pgm/epgm | ✓ | ✓ | ✓ | - |
| norm | ✓ | ✓ | ✓ | - |
| udp | ✓ | ✓ | ✓ | - |

## Socket Type Support by Version

| Socket Type | v0.1.1 | v0.1.2 | v0.1.3 | v0.2.0 |
|-------------|--------|--------|--------|--------|
| PAIR | ✓ | ✓ | ✓ | ✓ |
| PUB/SUB | ✓ | ✓ | ✓ | ✓ |
| XPUB/XSUB | ✓ | ✓ | ✓ | ✓ |
| DEALER/ROUTER | ✓ | ✓ | ✓ | ✓ |
| REQ/REP | ✓ | ✓ | - | - |
| PUSH/PULL | ✓ | ✓ | - | - |
| STREAM | ✓ | ✓ | ✓ | - |
| Draft sockets* | ✓ | - | - | - |

*Draft sockets: SERVER, CLIENT, RADIO, DISH, GATHER, SCATTER, DGRAM, PEER, CHANNEL

## Encryption Support by Version

| Encryption | v0.1.1 | v0.1.2 | v0.1.3 | v0.2.0 |
|------------|--------|--------|--------|--------|
| CURVE | - | - | - | - |
| TLS | - | - | - | ✓ |

---

[0.2.0]: https://github.com/kairos-code-dev/zlink/compare/v0.1.3...v0.2.0
[0.1.3]: https://github.com/kairos-code-dev/zlink/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/kairos-code-dev/zlink/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/kairos-code-dev/zlink/releases/tag/v0.1.1
