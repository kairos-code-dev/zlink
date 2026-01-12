# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Agent Preferences

- **Always use `dev-cxx` agent** for all development tasks
- All code modifications, build script changes, and CI/CD updates should be delegated to the dev-cxx agent

## Project Overview

This is **zlink** - a cross-platform native build system for libzmq (ZeroMQ) v4.3.5. It produces minimal pre-built native libraries with focused protocol support and modern TLS capabilities.

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
- SOCKS: Proxy support

**Other:**
- CURVE encryption (use TLS instead)
- Draft socket options: ZMQ_RECONNECT_STOP, ZMQ_SOCKS_USERNAME/PASSWORD, ZMQ_ZAP_ENFORCE_DOMAIN, etc.
- ZMQ_EVENT_PIPES_STATS and zmq_socket_monitor_pipes_stats()

### Target Platforms
- Windows: x64, ARM64
- Linux: x64, ARM64
- macOS: x86_64, ARM64

## TLS Configuration

zlink includes native TLS support using OpenSSL. Both native TLS transport (`tls://`) and WebSocket TLS (`wss://`) are available.

### TLS Transport Usage

**Server Setup:**
```c
void *ctx = zmq_ctx_new();
void *socket = zmq_socket(ctx, ZMQ_DEALER);

// Set server certificate and private key
zmq_setsockopt(socket, ZMQ_TLS_CERT, "/path/to/server-cert.pem", strlen("/path/to/server-cert.pem"));
zmq_setsockopt(socket, ZMQ_TLS_KEY, "/path/to/server-key.pem", strlen("/path/to/server-key.pem"));

// Bind to TLS endpoint
zmq_bind(socket, "tls://*:5555");
```

**Client Setup:**
```c
void *ctx = zmq_ctx_new();
void *socket = zmq_socket(ctx, ZMQ_DEALER);

// Set CA certificate for server verification
zmq_setsockopt(socket, ZMQ_TLS_CA, "/path/to/ca-cert.pem", strlen("/path/to/ca-cert.pem"));

// Set expected server hostname (for certificate validation)
zmq_setsockopt(socket, ZMQ_TLS_HOSTNAME, "server.example.com", strlen("server.example.com"));

// Connect to TLS endpoint
zmq_connect(socket, "tls://server.example.com:5555");
```

### WebSocket TLS (wss://)

WebSocket TLS uses the same certificate options as native TLS:

```c
// Server
zmq_setsockopt(socket, ZMQ_TLS_CERT, "/path/to/cert.pem", ...);
zmq_setsockopt(socket, ZMQ_TLS_KEY, "/path/to/key.pem", ...);
zmq_bind(socket, "wss://*:8080");

// Client
zmq_setsockopt(socket, ZMQ_TLS_CA, "/path/to/ca.pem", ...);
zmq_connect(socket, "wss://server.example.com:8080");
```

### TLS Socket Options

| Option | Type | Description |
|--------|------|-------------|
| ZMQ_TLS_CERT | string | Path to certificate file (PEM format) |
| ZMQ_TLS_KEY | string | Path to private key file (PEM format) |
| ZMQ_TLS_CA | string | Path to CA certificate for verification |
| ZMQ_TLS_HOSTNAME | string | Expected server hostname for validation |

## Build Requirements

### Required Dependencies
- **OpenSSL**: Required for TLS support (TLS and WebSocket TLS)
- **CMake**: 3.10 or higher
- **C++11 Compiler**: GCC 5+, Clang 3.8+, MSVC 2015+

### Build Configuration
The following options are enabled by default:
- `WITH_ASIO=ON`: Use ASIO backend (bundled Boost.Asio)
- `WITH_ASIO_SSL=ON`: Enable TLS support via OpenSSL
- `ENABLE_WS=ON`: Enable WebSocket transport
- `BUILD_STATIC=OFF`: Build shared libraries
- `BUILD_TESTS=ON`: Build test suite
- `ENABLE_CURVE=OFF`: CURVE encryption disabled

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
cmake -B build -DZMQ_CXX_STANDARD=20
cmake --build build

# Supported values: 11, 14, 17, 20, 23, latest (MSVC)
# Default is C++11 if not specified

# Combined with other options
cmake -B build -DZMQ_CXX_STANDARD=20 -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
```

See [CXX20_BUILD_EXAMPLES.md](CXX20_BUILD_EXAMPLES.md) for detailed C++ standard build instructions.

### Build Output
- Linux: `dist/linux-{arch}/libzmq.so`
- macOS: `dist/macos-{arch}/libzmq.dylib`
- Windows: `dist/windows-{arch}/libzmq.dll`

## Running Tests

Tests are integrated into build scripts via `RUN_TESTS=ON` parameter. Tests use libzmq's Unity test framework and run via ctest.

```bash
# Linux/macOS - run tests during build
./build-scripts/linux/build.sh x64 ON

# Run ctest directly in build directory
cd build/linux-x64 && ctest --output-on-failure
```

**Test Count**: 64 tests (5 skipped: fuzzer tests only)

**Note**: TIPC tests have been removed along with TIPC protocol support.

## Architecture

### Build System
- **VERSION file**: Defines LIBZMQ_VERSION
- **build-scripts/**: Platform-specific build scripts that configure and compile minimal libzmq
- **tests/**: libzmq test suite using Unity framework

### CI/CD
- **`.github/workflows/build.yml`**: GitHub Actions workflow building all 6 platforms
- Creates releases with zipped artifacts on tag pushes

### Key Files
| File | Purpose |
|------|---------|
| `VERSION` | Build version configuration |
| `CMakeLists.txt` | libzmq CMake configuration |
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
- `ZMQ_STREAM` → Use WebSocket (`ws://`, `wss://`) for stream-like communication
- `ZMQ_REQ/REP`, `ZMQ_PUSH/PULL` → Already removed in v0.1.3

**CURVE Encryption:**
- CURVE is no longer available
- Use TLS transport (`tls://`) or WebSocket TLS (`wss://`) instead
- See TLS Configuration section above for migration examples

**New Features:**
- Native TLS transport: `tls://`
- WebSocket support: `ws://`, `wss://`
- ASIO backend for improved I/O performance
- TLS socket options: `ZMQ_TLS_CERT`, `ZMQ_TLS_KEY`, `ZMQ_TLS_CA`, `ZMQ_TLS_HOSTNAME`

## Version History

| Version | Changes |
|---------|---------|
| v0.2.0 | Add ASIO backend, TLS/WebSocket support; remove STREAM, TIPC, VMCI, PGM, NORM, UDP protocols; remove CURVE |
| v0.1.3 | Remove REQ/REP, PUSH/PULL socket types and ZMQ_EVENT_PIPES_STATS |
| v0.1.2 | Remove all Draft API (9 socket types, WebSocket, draft options) |
| v0.1.1 | Initial release with full libzmq 4.3.5 |
