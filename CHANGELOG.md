# Changelog

All notable changes to zlink will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Removed

**Build System Cleanup**
- Removed Autotools build system (configure.ac, acinclude.m4, Makefile.am files)
- Removed GYP build configuration (builds/gyp/project.gyp)
- Removed MinGW/Cygwin Makefiles (Makefile.mingw32, Makefile.cygwin, README.cygwin.md)
- **CMake is now the only supported build system**

**Rationale:**
- CMake already supports all target platforms (Windows, Linux, macOS)
- Legacy build files referenced removed source files (req.cpp, pgm_socket.cpp, etc.)
- Legacy build files supported removed features (CURVE, TIPC, PGM, NORM, VMCI, UDP)
- Reduces technical debt by ~3,800 lines
- Simplifies maintenance and onboarding

### Changed

**Build Scripts**
- Updated 4 CI/CD scripts to use CMake instead of autotools:
  - ci_build.sh - Main CI build script
  - builds/cmake/ci_build.sh - CMake-specific builds
  - builds/coverage/ci_build.sh - Code coverage testing
  - builds/valgrind/ci_build.sh - Memory testing
- Documented 4 scripts that retain autotools for dependency builds:
  - builds/abi-compliance-checker/ci_build.sh - Upstream libzlink ABI checks
  - builds/fuzz/ci_build.sh - OSS-Fuzz integration
  - builds/android/build.sh - Android NDK dependency builder
  - builds/android/android_build_helper.sh - Generic autotools helper

### Migration Guide

If you were using Autotools or other legacy build systems:

**Before:**
```bash
./autogen.sh
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

All platforms (Windows, Linux, macOS) now use CMake exclusively. See build-scripts/ directory for platform-specific build scripts, or refer to CLAUDE.md for detailed build instructions.

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

### Migration Details

This migration was completed in 5 phases:
- **Phase 1:** Transport Layer cleanup (session_base.cpp, socket_base.cpp)
- **Phase 2:** I/O Thread Layer cleanup (io_thread.hpp, io_thread.cpp)
- **Phase 3:** Build system cleanup (CMakeLists.txt, poller.hpp)
- **Phase 4:** Documentation updates
- **Phase 5:** Final validation and performance testing

For detailed migration documentation, see `docs/team/20260115_asio-only/`

### Technical Details

- Total conditional compilation blocks removed: 10
- Total phase references removed: 20
- Test coverage: 61/61 tests passing (100%)
- Binary size: Unchanged (5.9 MB)
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

## [0.1.3] - 2024-XX-XX

### Removed
- **Socket Types**:
  - `ZLINK_REQ/REP`: Request-reply pattern
  - `ZLINK_PUSH/PULL`: Pipeline pattern
- **Monitoring**:
  - `ZLINK_EVENT_PIPES_STATS` event
  - `zlink_socket_monitor_pipes_stats()` function

## [0.1.2] - 2024-XX-XX

### Removed
- **Draft API**: Completely removed all draft socket types and options
  - Socket types: SERVER, CLIENT, RADIO, DISH, GATHER, SCATTER, DGRAM, PEER, CHANNEL
  - WebSocket transport (was draft feature, re-added in v0.2.0 as stable)
  - Draft socket options: `ZLINK_RECONNECT_STOP`, `ZLINK_ZAP_ENFORCE_DOMAIN`, etc.

## [0.1.1] - 2024-XX-XX

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
- Boost.Asio (bundled in `external/boost/`)

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

[0.2.0]: https://github.com/ulalax/zlink/compare/v0.1.3...v0.2.0
[0.1.3]: https://github.com/ulalax/zlink/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/ulalax/zlink/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/ulalax/zlink/releases/tag/v0.1.1
