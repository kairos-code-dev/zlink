# Supported Platforms

**zlink** is a simplified, cross-platform build of libzlink. It is actively developed and tested on the following platforms.

## Primary Support

These platforms are verified via CI/CD and are considered stable for production use.

| Operating System | Architectures | Compiler | Build System | Status |
|------------------|---------------|----------|--------------|--------|
| **Linux** (Ubuntu/Debian/RHEL) | x64 (AMD64), ARM64 (aarch64) | GCC, Clang | CMake | ✅ Stable |
| **macOS** | x64 (Intel), ARM64 (Apple Silicon) | Apple Clang | CMake | ✅ Stable |
| **Windows** (10/11/Server) | x64, ARM64 | MSVC (Visual Studio) | CMake | ✅ Stable |

## Notes

*   **Android**: Supported via cross-compilation (autotools/CMake), but secondary CI coverage.
*   **iOS**: Supported via cross-compilation (CMake), secondary CI coverage.
*   **Legacy Platforms**: Older platforms like Solaris, AIX, HP-UX, QNX (pre-7.0), and Windows CE are **not actively tested** with zlink configuration, though the underlying libzlink code may still support them. Use at your own risk.

## Architectures

*   **x64 (AMD64/x86_64)**: Primary development and deployment target.
*   **ARM64 (aarch64)**: Fully supported on all OSes (Linux, macOS, Windows).