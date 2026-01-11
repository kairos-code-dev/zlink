# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Agent Preferences

- **Always use `dev-cxx` agent** for all development tasks
- All code modifications, build script changes, and CI/CD updates should be delegated to the dev-cxx agent

## Project Overview

This is **zlink** - a cross-platform native build system for libzmq (ZeroMQ) v4.3.5. It produces minimal pre-built native libraries without CURVE encryption support.

**Note**: Draft API has been completely removed from this build. Only stable socket types are available.

### Supported Socket Types
| Type | Description |
|------|-------------|
| PAIR | Exclusive pair for bidirectional communication |
| PUB/SUB | Publish-subscribe pattern |
| XPUB/XSUB | Extended pub-sub with subscription forwarding |
| DEALER/ROUTER | Async request-reply pattern |
| STREAM | Raw TCP stream |

### Removed Features
**Socket Types:**
- Draft API: SERVER, CLIENT, RADIO, DISH, GATHER, SCATTER, DGRAM, PEER, CHANNEL
- REQ/REP: Request-reply pattern
- PUSH/PULL: Pipeline pattern

**Other:**
- WebSocket transport (ws://, wss://)
- Draft socket options: ZMQ_RECONNECT_STOP, ZMQ_SOCKS_USERNAME/PASSWORD, ZMQ_ZAP_ENFORCE_DOMAIN, etc.
- ZMQ_EVENT_PIPES_STATS and zmq_socket_monitor_pipes_stats()
- NORM protocol support

### Target Platforms
- Windows: x64, ARM64
- Linux: x64, ARM64
- macOS: x86_64, ARM64

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

**Test Count**: 67 tests (8 skipped: 5 fuzzer, 3 TIPC)

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

- **TIPC tests**: Linux-only, skipped on other platforms
- **IPC tests**: Not available on Windows
- **Windows ARM64**: Cross-compiled on x64 host, tests cannot run
- **Fuzzer tests**: Require special build configuration, typically skipped

## Version History

| Version | Changes |
|---------|---------|
| v0.1.3 | Remove REQ/REP, PUSH/PULL socket types and ZMQ_EVENT_PIPES_STATS |
| v0.1.2 | Remove all Draft API (9 socket types, WebSocket, draft options) |
| v0.1.1 | Initial release with full libzmq 4.3.5 |
