# zlink: Simplified ZeroMQ Build

[![Build Status](https://github.com/zeromq/libzmq/actions/workflows/CI.yaml/badge.svg)](https://github.com/zeromq/libzmq/actions/workflows/CI.yaml)

**zlink** is a cross-platform, streamlined native build of **libzmq (ZeroMQ) v4.3.5**. It is designed for modern transport protocols with minimal dependencies and a lightweight footprint.

## Key Features

- **ASIO-based I/O**: Boost.Asio backend (bundled, no external dependency) for high-performance async I/O.
- **TLS Support**: Native TLS transport (`tls://`) and WebSocket TLS (`wss://`) via OpenSSL.
- **WebSocket**: Standard WebSocket transport (`ws://`, `wss://`) for web compatibility.
- **Simplified Protocols**: Focused set of transports: `tcp`, `ipc`, `inproc`, `ws`, `wss`, `tls`.
- **Minimal Dependencies**:
    - No `libsodium` (CURVE removed).
    - No `libbsd` or other platform-specific extras.
    - No multicast protocols (PGM, EPGM, NORM).
    - No legacy transports (TIPC, VMCI, UDP).

## Supported Platforms

zlink is actively tested and supported on the following platforms:

| Platform | Architectures | Compiler | Build System | Status |
|----------|---------------|----------|--------------|--------|
| **Linux** | x64, ARM64 | GCC / Clang | CMake | ✅ Stable |
| **macOS** | x64, ARM64 | Apple Clang | CMake | ✅ Stable |
| **Windows**| x64, ARM64 | MSVC | CMake | ✅ Stable |

## Supported Socket Types

zlink supports a core set of socket patterns required for modern distributed systems:

| Type | Description |
|------|-------------|
| **PAIR** | Exclusive pair pattern for 1-to-1 communication. |
| **PUB / SUB** | Publish-subscribe pattern for data distribution. |
| **XPUB / XSUB** | Extended pub-sub with subscription forwarding. |
| **DEALER / ROUTER** | Asynchronous request-reply, load balancing, and explicit routing. |

## Removed Features

To maintain simplicity and size, the following features from standard libzmq are **removed**:

*   **Socket Types:** `REQ`, `REP`, `PUSH`, `PULL`, `STREAM`, and all DRAFT sockets (Server, Client, Radio, Dish, etc.).
*   **Protocols:** `udp`, `tipc`, `vmci`, `pgm`, `epgm`, `norm`.
*   **Encryption:** CURVE (libsodium) is removed. Use standard TLS (`tls://` or `wss://`) instead.

## Build Instructions

### Prerequisites

*   **CMake** 3.10+
*   **C++11 Compiler** (GCC 5+, Clang 3.8+, MSVC 2015+)
*   **OpenSSL** (Required for TLS support)

### OpenSSL Setup (Recommended)

Keep OpenSSL external (OS package manager or vcpkg). Vendoring is not
recommended due to security updates and platform-specific builds.

**Windows (vcpkg):**
```powershell
.\vcpkg install openssl:x64-windows
# Configure CMake with:
# -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install libssl-dev
```

**macOS (Homebrew):**
```bash
brew install openssl@3
# Configure CMake with:
# -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
```

### Quick Build Scripts

We provide platform-specific scripts that handle configuration and testing:

```bash
# Linux
./build-scripts/linux/build.sh x64 ON

# macOS
./build-scripts/macos/build.sh arm64 ON

# Windows (PowerShell)
.\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
```

### Manual CMake Build

```bash
cmake -B build \
    -DWITH_TLS=ON \
    -DBUILD_TESTS=ON \
    -DBUILD_BENCHMARKS=OFF \
    -DBUILD_SHARED=ON
cmake --build build
```

### Windows (VS 2022 / CLion)

Prereqs: Visual Studio 2022 (Desktop C++), CMake, OpenSSL.
If OpenSSL is installed via vcpkg, set `CMAKE_TOOLCHAIN_FILE` to the vcpkg toolchain.

```powershell
cmake -S . -B build\win ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DWITH_TLS=ON -DBUILD_TESTS=ON -DBUILD_SHARED=ON ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build\win --config Release
ctest --test-dir build\win --output-on-failure -C Release
```

### Linux

Prereqs (Ubuntu/Debian): `sudo apt-get install build-essential cmake pkg-config libssl-dev`

```bash
cmake -S . -B build/linux \
  -DWITH_TLS=ON -DBUILD_TESTS=ON -DBUILD_SHARED=ON
cmake --build build/linux
ctest --test-dir build/linux --output-on-failure
```

### Configuration Options

*   `WITH_TLS` (Default: `ON`): Enable OpenSSL-based TLS.
*   `BUILD_TESTS` (Default: `ON`): Build the Unity-based test suite.
*   `BUILD_BENCHMARKS` (Default: `OFF`): Build performance comparison tools.

## Distribution and Consumption

To support multiple environments, provide more than one consumption path.
There is no single universal mechanism for C/C++.

**Recommended options:**
*   **Source build (CMake):** Works everywhere; use `find_package` for deps.
*   **vcpkg / conan:** Easiest for developers on Windows and cross-platform.
*   **OS packages:** deb/rpm for Linux distributions.
*   **Prebuilt SDK:** zip/tar with `include/`, `lib/`, and required DLL/so.

**Typical dependency flows:**
*   OpenSSL via system package manager or vcpkg.
*   Bundled Boost headers already live in `external/boost`.

## TLS Usage

**zlink** uses standard OpenSSL for encryption.

**Server Example:**
```c
zmq_setsockopt(socket, ZMQ_TLS_CERT, "/path/to/cert.pem", ...);
zmq_setsockopt(socket, ZMQ_TLS_KEY, "/path/to/key.pem", ...);
zmq_bind(socket, "tls://*:5555");
```

**Client Example:**
```c
zmq_setsockopt(socket, ZMQ_TLS_CA, "/path/to/ca.pem", ...);
zmq_setsockopt(socket, ZMQ_TLS_HOSTNAME, "server.example.com", ...);
zmq_connect(socket, "tls://server.example.com:5555");
```

See [doc/TLS_USAGE_GUIDE.md](doc/TLS_USAGE_GUIDE.md) for detailed configuration.

## Benchmarks

This repository includes a `benchwithzmq/` directory to compare zlink performance against standard libzmq.

```bash
# Run comparison benchmarks (Linux/macOS)
python3 benchwithzmq/run_comparison.py ALL --runs 3
```

Results are output to `benchwithzmq/`.

## Documentation Resources

*   [CLAUDE.md](CLAUDE.md): Detailed project overview and agent context.
*   [GEMINI.md](GEMINI.md): Context for Gemini agent.
*   [doc/TLS_USAGE_GUIDE.md](doc/TLS_USAGE_GUIDE.md): TLS instructions.
*   [CXX20_BUILD_EXAMPLES.md](CXX20_BUILD_EXAMPLES.md): C++ standard selection guide.

## License & Attribution

**zlink** is based on [ZeroMQ (libzmq)](https://github.com/zeromq/libzmq).

The project license is specified in [LICENSE](LICENSE) (Mozilla Public License Version 2.0).

Original Libzmq Resources:
*   Website: http://www.zeromq.org/
*   Git repository: http://github.com/zeromq/libzmq
