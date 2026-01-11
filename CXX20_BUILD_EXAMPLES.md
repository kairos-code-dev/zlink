# C++20 Build Examples for zlink

## Overview

zlink now supports building with C++20 (or any other C++ standard) via the `ZMQ_CXX_STANDARD` CMake option.

## Build Options

### Linux/macOS with C++20

```bash
# Configure with C++20
cmake -B build -DZMQ_CXX_STANDARD=20

# Build
cmake --build build
```

### Windows with C++20 (Visual Studio)

```powershell
# Configure with C++20
cmake -B build -DZMQ_CXX_STANDARD=20

# Build
cmake --build build --config Release
```

### Supported C++ Standards

- **11** - C++11 (default if no option specified)
- **14** - C++14
- **17** - C++17
- **20** - C++20
- **23** - C++23 (if supported by compiler)
- **latest** - Latest supported by compiler (MSVC only)

### Examples with Different Standards

```bash
# C++14
cmake -B build -DZMQ_CXX_STANDARD=14

# C++17
cmake -B build -DZMQ_CXX_STANDARD=17

# C++20
cmake -B build -DZMQ_CXX_STANDARD=20

# C++23 (if compiler supports)
cmake -B build -DZMQ_CXX_STANDARD=23
```

### Combined with Other Options

```bash
# C++20 with tests enabled
cmake -B build -DZMQ_CXX_STANDARD=20 -DBUILD_TESTS=ON

# C++20 with benchmarks enabled
cmake -B build -DZMQ_CXX_STANDARD=20 -DBUILD_BENCHMARKS=ON

# C++20 with Release build type
cmake -B build -DZMQ_CXX_STANDARD=20 -DCMAKE_BUILD_TYPE=Release
```

### Using Build Scripts

#### Linux

```bash
# Modify build-scripts/linux/build.sh to add:
# cmake -B "$BUILD_DIR" \
#   -DZMQ_CXX_STANDARD=20 \
#   -DCMAKE_BUILD_TYPE=Release \
#   ...

./build-scripts/linux/build.sh x64 ON
```

#### macOS

```bash
# Modify build-scripts/macos/build.sh similarly
./build-scripts/macos/build.sh arm64 ON
```

#### Windows

```powershell
# Modify build-scripts/windows/build.ps1 to add:
# cmake -B "$buildDir" `
#   -DZMQ_CXX_STANDARD=20 `
#   -G "Visual Studio 17 2022" `
#   ...

.\build-scripts\windows\build.ps1 -Architecture x64 -RunTests "ON"
```

## Compiler Requirements

### Minimum Compiler Versions for C++20

| Compiler | Minimum Version |
|----------|----------------|
| GCC      | 10.0           |
| Clang    | 10.0           |
| MSVC     | 19.29 (VS 2019 16.11) |
| Apple Clang | 13.0        |

### Verification

Check if your compiler supports C++20:

```bash
# GCC
g++ --version
g++ -std=c++20 -E -x c++ - < /dev/null > /dev/null 2>&1 && echo "C++20 supported" || echo "C++20 not supported"

# Clang
clang++ --version
clang++ -std=c++20 -E -x c++ - < /dev/null > /dev/null 2>&1 && echo "C++20 supported" || echo "C++20 not supported"

# MSVC (Windows)
cl.exe /?
```

## Fallback Behavior

- If the specified C++ standard is not supported by the compiler, the build will:
  - **Linux/macOS**: Display a warning and fall back to C++11
  - **MSVC**: Display a warning and use compiler default

- If `ZMQ_CXX_STANDARD` is not specified, the build uses:
  - **Linux/macOS**: C++11
  - **MSVC**: Compiler default (typically C++14)

## Notes

1. **Binary Compatibility**: Changing the C++ standard may affect ABI compatibility. Ensure all dependent libraries use compatible standards.

2. **Feature Usage**: zlink's core code uses C++11 features. Higher standards allow users to link zlink with their C++17/20 projects without ABI issues.

3. **CMake Cache**: If changing standards, clean the build directory or use `-U ZMQ_CXX_STANDARD` to force reconfiguration:
   ```bash
   cmake -B build -U ZMQ_CXX_STANDARD -DZMQ_CXX_STANDARD=20
   ```

4. **Per-Platform Differences**:
   - GCC/Clang use `-std=c++20`
   - MSVC uses `/std:c++20`
   - The CMake configuration handles these differences automatically

## Troubleshooting

### Error: Compiler doesn't support C++20

```
CMake Warning:
  Compiler does not support C++20, falling back to C++11
```

**Solution**: Update your compiler to a version that supports C++20 (see table above).

### Error: Unknown flag `-std=c++20`

**Solution**: Your compiler is too old. Update to a newer version or use a lower C++ standard:

```bash
cmake -B build -DZMQ_CXX_STANDARD=17
```

### Cache issues when switching standards

**Solution**: Clean build directory or remove CMake cache:

```bash
rm -rf build
cmake -B build -DZMQ_CXX_STANDARD=20
```
