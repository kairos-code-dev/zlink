# Gemini Context: zlink (libzmq custom build)

## Project Overview
**zlink** is a cross-platform native build system for **libzmq (ZeroMQ) v4.3.5**. It produces pre-built minimal native libraries.

**Key Characteristics:**
*   **Minimal API:** Draft APIs AND standard REQ/REP, PUSH/PULL socket types have been completely removed.
*   **No Encryption:** Removed libsodium and CURVE support for a lightweight footprint.
*   **Platforms:** Linux, macOS, Windows (supporting both x64 and ARM64).
*   **Language:** C++ (primarily C++98 with optional C++11 fragments).

## Architecture & Directory Structure
*   **`build-scripts/`**: Platform-specific scripts to download, configure, and compile libzmq + libsodium.
    *   `linux/build.sh`, `macos/build.sh`, `windows/build.ps1`
*   **`src/`**: Core libzmq source code.
*   **`include/`**: Public headers (`zmq.h`).
*   **`tests/`**: Test suite using the Unity framework.
*   **`VERSION`**: Configuration file defining versions for libzmq, libsodium, and features.
*   **`CMakeLists.txt`**: Main build configuration.

## Building and Running

### Standard Local Build (Linux)
Use the provided `build.sh` in the root directory for a quick local clean build and test run:
```bash
./build.sh
```

### Platform-Specific Builds
Scripts in `build-scripts/` allow for more granular control (arch, versions).

**Linux:**
```bash
./build-scripts/linux/build.sh [ARCH] [LIBZMQ_VERSION] [LIBSODIUM_VERSION] [ENABLE_CURVE] [RUN_TESTS]
# Example: ./build-scripts/linux/build.sh x64 4.3.5 1.0.20 ON ON
```

**macOS:**
```bash
./build-scripts/macos/build.sh [ARCH] [LIBZMQ_VERSION] [LIBSODIUM_VERSION] [ENABLE_CURVE] [RUN_TESTS]
```

**Windows (PowerShell):**
```powershell
.\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
```

### Running Tests
Tests are typically run as part of the build scripts if `RUN_TESTS=ON`.
To run manually after a build:
```bash
cd build
ctest --output-on-failure
```
Current status: **61 tests passed** (100% success rate for functional tests).

### Performance Comparison
A comprehensive benchmark suite is available in `benchwithzmq/`.
```bash
python3 benchwithzmq/run_comparison.py ALL
```
Results are saved in `benchwithzmq/wsl_COMPARISON_RESULTS.md` (conducted in WSL environment).

## Development Conventions
*   **Optimization**: C++11 + LTO (Link Time Optimization) is used to achieve maximum performance.
*   **Migration**: C++20 migration roadmap is defined in `doc/CPP20_MIGRATION_RESEARCH.md`.
*   **Contribution Process**: Follows the [C4 (Collective Code Construction Contract)](https://rfc.zeromq.org/spec:42/C4/).
*   **Code Style**: Adhere to existing C++98/C++11 patterns found in `src/`.
*   **Testing**: All new features or fixes must include relevant tests in `tests/`.
*   **Agent Persona**: Addressed as "사장님" (Sajangnim) by the user.

## Supported Socket Types
*   **PAIR**: Exclusive pair.
*   **PUB/SUB, XPUB/XSUB**: Publish-subscribe.
*   **DEALER/ROUTER**: Async request-reply (Load balancing / Explicit routing).
*   **STREAM**: Raw TCP.
*   **ROUTER-to-ROUTER**: P2P and Broker patterns (Verified in benchmarks).

## Memories
*   `msg_t` MUST be exactly 64 bytes and trivially copyable to survive bitwise moves in `ypipe_t` and cross-boundary calls.
*   **LTO (Link Time Optimization)** is critical for `inproc` performance; ensure `-flto` is enabled in `CMakeLists.txt`.
*   Always perform a `git pull --rebase` before pushing to avoid history conflicts.
