# Gemini Context: zlink (libzmq custom build)

## Project Overview
**zlink** is a cross-platform native build system for **libzmq (ZeroMQ) v4.3.5**. It produces pre-built minimal native libraries optimized for high performance and low footprint.

**Key Characteristics:**
*   **Minimal API:** Draft APIs AND standard REQ/REP, PUSH/PULL socket types have been completely removed.
*   **No Encryption:** Removed libsodium and CURVE support for a lightweight footprint.
*   **API Modernization:** Legacy APIs (`zmq_init`, `zmq_term`, `zmq_ctx_destroy`) have been replaced with modern equivalents.
*   **Platforms:** Linux, macOS, Windows (supporting both x64 and ARM64).
*   **Language:** C++ (primarily C++98 with C++11/C++14/C++17/C++20 fragments as needed by build environment).

## Architecture & Directory Structure
*   **`benchwithzmq/`**: Precise performance comparison system against standard libzmq.
*   **`build-scripts/`**: Platform-specific scripts to download, configure, and compile libzmq.
*   **`src/`**: Core libzmq source code (Cleaned from Sodium and legacy remnants).
*   **`include/`**: Public headers (`zmq.h`).
*   **`tests/`**: Test suite using the Unity framework.
*   **`VERSION`**: Configuration file defining versions and features.

## Building and Running

### Standard Local Build (Linux)
Use the provided `build.sh` in the root directory:
```bash
./build.sh
```

### Running Tests
Tests are typically run as part of the build scripts if `RUN_TESTS=ON`.
To run manually after a build:
```bash
cd build/linux-x64  # or relevant build dir
ctest --output-on-failure
```

### Performance Comparison
A comparative benchmark against standard **libzmq** is available in the root.
```bash
# Run specific pattern (10 iterations with outlier removal)
python3 benchwithzmq/run_comparison.py PAIR
python3 benchwithzmq/run_comparison.py DEALER_ROUTER

# Run all and save results
python3 benchwithzmq/run_comparison.py ALL | tee benchwithzmq/COMPARISON_RESULTS.md
```

## Version History
*   **v0.1.3**: Complete removal of Sodium/CURVE, legacy APIs, and unused test remnants. Added `benchwithzmq` comparison tool.
*   **v0.1.2**: Remove all Draft API (9 socket types, WebSocket, draft options).
*   **v0.1.1**: Initial release with full libzmq 4.3.5.

## Supported Socket Types
*   **PAIR**: Exclusive pair.
*   **PUB/SUB, XPUB/XSUB**: Publish-subscribe.
*   **DEALER/ROUTER**: Async request-reply (Load balancing / Explicit routing).
*   **STREAM**: Raw TCP.

## Memories
*   `msg_t` MUST be exactly 64 bytes and trivially copyable to survive bitwise moves in `ypipe_t` and cross-boundary calls.