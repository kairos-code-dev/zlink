# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Agent Preferences

- **Always use `dev-cxx` agent** for all development tasks
- All code modifications, build script changes, and CI/CD updates should be delegated to the dev-cxx agent

## Project Overview

This is **zlink** - a cross-platform native build system for libzlink (Zlink) v4.3.5. It produces minimal pre-built native libraries with focused protocol support and modern TLS capabilities.

**Key Features:**
- ASIO-based I/O backend (using bundled Boost.Asio)
- WebSocket support with TLS encryption (ws://, wss://)
- Native TLS transport (tls://) using OpenSSL
- Simplified protocol stack focused on modern use cases

### Supported Protocols
| Protocol | Description | Platforms |
|----------|-------------|-----------|
| tcp | Standard TCP transport | All |
| ipc | Inter-process communication | Unix/Linux/macOS |
| inproc | In-process messaging | All |
| ws | WebSocket transport | All |
| wss | WebSocket with TLS | All |
| tls | Native TLS transport | All |

### Supported Socket Types
| Type | Description |
|------|-------------|
| PAIR | Exclusive pair for bidirectional communication |
| PUB/SUB | Publish-subscribe pattern |
| XPUB/XSUB | Extended pub-sub with subscription forwarding |
| DEALER/ROUTER | Async request-reply pattern |

### Removed Features
**Socket Types:**
- Draft API: SERVER, CLIENT, RADIO, DISH, GATHER, SCATTER, DGRAM, PEER, CHANNEL
- REQ/REP: Request-reply pattern
- PUSH/PULL: Pipeline pattern
- STREAM: Raw TCP stream (use WebSocket for stream-like behavior)

**Protocols:**
- TIPC: Transparent Inter-Process Communication
- VMCI: VMware Virtual Machine Communication Interface
- PGM/EPGM: Pragmatic General Multicast
- NORM: NACK-Oriented Reliable Multicast
- UDP: Unicast and multicast UDP

**Other:**
- CURVE encryption (use TLS instead)
- Draft socket options: ZLINK_RECONNECT_STOP, ZLINK_ZAP_ENFORCE_DOMAIN, etc.
- ZLINK_EVENT_PIPES_STATS and zlink_socket_monitor_pipes_stats()

### Target Platforms
- Windows: x64, ARM64
- Linux: x64, ARM64
- macOS: x86_64, ARM64

## Performance Notes

### Transport Performance Characteristics (Phase 5 - Stable)

**Comprehensive Benchmark Results (10 runs, Linux x64):**

| Pattern | TCP 64B | IPC 64B | inproc 64B |
|---------|---------|---------|------------|
| DEALER_DEALER | 6.03 M/s | 5.96 M/s | 5.96 M/s |
| PAIR | 5.78 M/s | 5.65 M/s | **6.09 M/s** |
| DEALER_ROUTER | 5.40 M/s | 5.55 M/s | 5.40 M/s |
| PUBSUB | 5.76 M/s | 5.70 M/s | 5.71 M/s |
| ROUTER_ROUTER | 5.03 M/s | 5.12 M/s | 4.71 M/s |
| ROUTER_ROUTER_POLL | 4.83 M/s | 5.04 M/s | 3.99 M/s |

**Key Findings:**
- **All transports achieve 4-6 M/s** for small messages (64B)
- **TCP/IPC/inproc perform similarly** for 64B messages (within ±5%)
- **inproc best for large messages** (1KB): zero-copy advantage
- **100% stability**: Zero deadlocks across all patterns and transports
- **libzlink parity**: ~99% performance vs standard libzlink (±1-3%)

**Latency Characteristics:**
- inproc: 0.07 ~ 0.5 μs (ultra-low)
- IPC: 15.6 ~ 37 μs (low)
- TCP: 16.7 ~ 42 μs (low)

**Recommendations:**
1. **Use inproc for same-process** - Ultra-low latency, zero-copy for large messages
2. **Use IPC for local inter-process** - Similar throughput to TCP, lower latency
3. **Use TCP for network** - Solid 5-6 M/s performance

**Reference:** See `doc/report/performance/tag_0.5_performance_report.md` for complete analysis.

## TLS Configuration

zlink includes native TLS support using OpenSSL. Both native TLS transport (`tls://`) and WebSocket TLS (`wss://`) are available.

### TLS Transport Usage

**Server Setup:**
```c
void *ctx = zlink_ctx_new();
void *socket = zlink_socket(ctx, ZLINK_DEALER);

// Set server certificate and private key
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/server-cert.pem", strlen("/path/to/server-cert.pem"));
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/server-key.pem", strlen("/path/to/server-key.pem"));

// Bind to TLS endpoint
zlink_bind(socket, "tls://*:5555");
```

**Client Setup:**
```c
void *ctx = zlink_ctx_new();
void *socket = zlink_socket(ctx, ZLINK_DEALER);

// Set CA certificate for server verification
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca-cert.pem", strlen("/path/to/ca-cert.pem"));

// Set expected server hostname (for certificate validation)
zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, "server.example.com", strlen("server.example.com"));

// Connect to TLS endpoint
zlink_connect(socket, "tls://server.example.com:5555");
```

### WebSocket TLS (wss://)

WebSocket TLS uses the same certificate options as native TLS:

```c
// Server
zlink_setsockopt(socket, ZLINK_TLS_CERT, "/path/to/cert.pem", ...);
zlink_setsockopt(socket, ZLINK_TLS_KEY, "/path/to/key.pem", ...);
zlink_bind(socket, "wss://*:8080");

// Client
zlink_setsockopt(socket, ZLINK_TLS_CA, "/path/to/ca.pem", ...);
zlink_connect(socket, "wss://server.example.com:8080");
```

### TLS Socket Options

| Option | Type | Description |
|--------|------|-------------|
| ZLINK_TLS_CERT | string | Path to certificate file (PEM format) |
| ZLINK_TLS_KEY | string | Path to private key file (PEM format) |
| ZLINK_TLS_CA | string | Path to CA certificate for verification |
| ZLINK_TLS_HOSTNAME | string | Expected server hostname for validation |

## Build Requirements

### Build System

**zlink uses CMake exclusively** for all platforms as of v0.3.1. Legacy build systems (Autotools, GYP, MinGW Makefiles) have been removed.

All build scripts in `build-scripts/` directory use CMake. The project requires CMake 3.10 or higher.

### Required Dependencies
- **OpenSSL**: Required for TLS support (TLS and WebSocket TLS)
- **CMake**: 3.10 or higher
- **C++11 Compiler**: GCC 5+, Clang 3.8+, MSVC 2015+

### Build Configuration

**Mandatory Components:**
- **ASIO backend**: Always enabled (bundled Boost.Asio headers, no external dependency)
- **WebSocket transport**: Always enabled (bundled Boost.Beast headers, no external dependency)

**Optional Components:**
- `WITH_TLS=ON`: Enable TLS and WSS transport support via OpenSSL (default: ON, can be disabled)
- `BUILD_STATIC=OFF`: Build shared libraries (default: OFF)
- `BUILD_TESTS=ON`: Build test suite (default: ON)

## Build Commands

### Linux
```bash
./build-scripts/linux/build.sh [ARCH] [RUN_TESTS]
# Example: ./build-scripts/linux/build.sh x64 ON
```

### macOS
```bash
./build-scripts/macos/build.sh [ARCH] [RUN_TESTS]
# Example: ./build-scripts/macos/build.sh arm64 ON
```

### Windows (PowerShell)
```powershell
.\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
```

### Build with Benchmarks
```bash
# Enable benchmark tools for performance comparison
cmake -B build -DBUILD_BENCHMARKS=ON
cmake --build build
```

### Build with C++20 (or other standards)
```bash
# Build with C++20 standard
cmake -B build -DZLINK_CXX_STANDARD=20
cmake --build build

# Supported values: 11, 14, 17, 20, 23, latest (MSVC)
# Default is C++11 if not specified

# Combined with other options
cmake -B build -DZLINK_CXX_STANDARD=20 -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
```

See [CXX_BUILD_EXAMPLES.md](CXX_BUILD_EXAMPLES.md) for detailed C++ standard build instructions.

### Build Output
- Linux: `dist/linux-{arch}/libzlink.so`
- macOS: `dist/macos-{arch}/libzlink.dylib`
- Windows: `dist/windows-{arch}/libzlink.dll`

## Running Tests

Tests are integrated into build scripts via `RUN_TESTS=ON` parameter. Tests use libzlink's Unity test framework and run via ctest.

```bash
# Linux/macOS - run tests during build
./build-scripts/linux/build.sh x64 ON

# Run ctest directly in build directory
cd build/linux-x64 && ctest --output-on-failure
```

**Test Count**: 64 tests (5 skipped: fuzzer tests only)

**Note**: TIPC tests have been removed along with TIPC protocol support.

### Transport Matrix Tests

The test suite includes a comprehensive transport matrix test (`test_transport_matrix`) that validates all socket patterns across all supported transports:

**Socket Patterns Tested:**
- PAIR: Bidirectional communication
- PUB/SUB: Publish-subscribe with empty filter
- ROUTER/DEALER: Identity-based routing with single dealer
- ROUTER/ROUTER: Bidirectional routing between routers

**Transport Coverage:**
- **tcp, inproc, ipc**: All socket patterns tested
  - IPC automatically skipped on Windows (not supported)
- **ws, wss, tls**: Only PAIR and PUB/SUB patterns tested
  - WebSocket and TLS transports are skipped if not available at runtime
  - Uses `zlink_has()` for runtime capability detection

**Specialized Tests (Not in Matrix):**
- **Topic Filtering**: `test_pubsub_filter_xpub` includes tests for `ZLINK_SUBSCRIBE` topic filtering
- **XPUB/XSUB**: `test_pubsub_filter_xpub` includes tests for extended pub/sub with subscription forwarding messages
- **Multiple Dealers**: `test_router_multiple_dealers` tests router handling multiple concurrent dealer connections

**Test File Organization:**
- `test_transport_matrix.cpp` - Comprehensive matrix of socket types × transports (basic patterns)
- `test_pubsub_filter_xpub.cpp` - PUB/SUB topic filtering and XPUB/XSUB advanced features
- `test_router_multiple_dealers.cpp` - ROUTER socket with multiple concurrent DEALER clients

The matrix test automatically adapts to platform capabilities, skipping unsupported transports gracefully with `TEST_IGNORE` status.

## Architecture

### Build System
- **VERSION file**: Defines LIBZLINK_VERSION
- **build-scripts/**: Platform-specific build scripts that configure and compile minimal libzlink
- **tests/**: libzlink test suite using Unity framework

### CI/CD
- **`.github/workflows/build.yml`**: GitHub Actions workflow building all 6 platforms
- Creates releases with zipped artifacts on tag pushes

### Key Files
| File | Purpose |
|------|---------|
| `VERSION` | Build version configuration |
| `CMakeLists.txt` | libzlink CMake configuration |
| `tests/CMakeLists.txt` | Test suite configuration |

## LSP (Language Server Protocol) 사용

코드 분석 시 LSP 도구를 활용하여 정확한 코드 탐색이 가능합니다.

### 지원 언어

| 언어 | LSP 서버 |
|------|----------|
| C/C++ | clangd |
| C# | omnisharp |
| Java | jdtls |
| Kotlin | kotlin-language-server |

### LSP 명령어

```bash
# 정의로 이동 (함수/클래스 정의 찾기)
LSP goToDefinition <file> <line> <column>

# 참조 찾기 (모든 사용처 검색)
LSP findReferences <file> <line> <column>

# 호버 정보 (타입, 문서 확인)
LSP hover <file> <line> <column>

# 문서 심볼 (파일 내 모든 심볼 목록)
LSP documentSymbol <file> <line> <column>

# 구현체 찾기 (인터페이스 구현 검색)
LSP goToImplementation <file> <line> <column>

# 호출 계층 (함수 호출 관계)
LSP incomingCalls <file> <line> <column>
LSP outgoingCalls <file> <line> <column>
```

### 사용 예시

```bash
# C++ 함수 정의 찾기
LSP goToDefinition src/socket_base.cpp 150 20

# 모든 참조 검색
LSP findReferences src/ctx.cpp 100 15
```

## Platform-Specific Notes

- **IPC tests**: Not available on Windows (IPC protocol is Unix-only)
- **Windows ARM64**: Cross-compiled on x64 host, tests cannot run
- **Fuzzer tests**: Require special build configuration, typically skipped
- **OpenSSL**: Required on all platforms for TLS support
  - Linux: Install via package manager (libssl-dev, openssl-devel)
  - macOS: Pre-installed or via Homebrew
  - Windows: Automatically fetched via vcpkg during CMake configuration

## Migration Guide

### From v0.1.3 to v0.2.0

**Removed Protocols:**
If you were using any of these protocols, you must migrate:
- `tipc://` → Use `tcp://` or `ipc://` for inter-process communication
- `vmci://` → Use `tcp://` for VM communication
- `pgm://`, `epgm://` → Use `tcp://` with application-level reliability
- `norm://` → Use `tcp://` with application-level multicast
- `udp://` → Use `tcp://` or WebSocket for datagram-like behavior

**Removed Socket Types:**
- `ZLINK_STREAM` → Use WebSocket (`ws://`, `wss://`) for stream-like communication
- `ZLINK_REQ/REP`, `ZLINK_PUSH/PULL` → Already removed in v0.1.3

**CURVE Encryption:**
- CURVE is no longer available
- Use TLS transport (`tls://`) or WebSocket TLS (`wss://`) instead
- See TLS Configuration section above for migration examples

**New Features:**
- Native TLS transport: `tls://`
- WebSocket support: `ws://`, `wss://`
- ASIO backend for improved I/O performance
- TLS socket options: `ZLINK_TLS_CERT`, `ZLINK_TLS_KEY`, `ZLINK_TLS_CA`, `ZLINK_TLS_HOSTNAME`

## Version History

| Version | Changes |
|---------|---------|
| v0.2.0 | Add ASIO backend, TLS/WebSocket support; remove STREAM, TIPC, VMCI, PGM, NORM, UDP protocols; remove CURVE |
| v0.1.3 | Remove REQ/REP, PUSH/PULL socket types and ZLINK_EVENT_PIPES_STATS |
| v0.1.2 | Remove all Draft API (9 socket types, WebSocket, draft options) |
| v0.1.1 | Initial release with full libzlink 4.3.5 |
