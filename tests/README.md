# libzmq Test Suite

Comprehensive validation tests for libzmq native build artifacts.

## Overview

This test suite validates that libzmq binaries are correctly built with:
- Static libsodium linkage
- CURVE encryption support
- Basic ZeroMQ functionality
- Cross-platform compatibility

## Test Files

### Test Programs

#### `test_basic.c`
Validates core libzmq functionality:
- Library version verification
- Context creation and termination
- Socket creation and destruction
- REQ/REP pattern message passing

**Expected Outcome**: Confirms libzmq loads and basic socket operations work correctly.

#### `test_curve.c`
Validates CURVE encryption support:
- CURVE mechanism availability check
- Keypair generation (zmq_curve_keypair)
- Encrypted socket communication
- Client-server CURVE handshake

**Expected Outcome**: Confirms libsodium is statically linked and CURVE encryption is fully functional.

### Build Configuration

#### `CMakeLists.txt`
Cross-platform CMake configuration:
- Automatic platform detection (Windows/Linux/macOS)
- Architecture detection (x64/x86/ARM64)
- Library path resolution from `dist/` directory
- Test executable compilation and linking

### Test Runners

#### `run_tests.sh` (Linux/macOS)
Automated test execution script:
- Platform and architecture detection
- CMake build configuration
- Test execution with colored output
- Summary report with validation status

**Usage**:
```bash
./run_tests.sh
```

#### `run_tests.ps1` (Windows)
PowerShell test execution script:
- Architecture selection (x64/x86)
- Visual Studio build integration
- Test execution with colored output
- Summary report with validation status

**Usage**:
```powershell
.\run_tests.ps1 -Architecture x64
```

## Prerequisites

### All Platforms
- CMake 3.15 or higher
- Built libzmq binaries in `dist/` directory

### Platform-Specific

**Windows**:
- Visual Studio 2019 or 2022 with C++ build tools
- PowerShell 5.0+

**Linux**:
- GCC 7.0+ or Clang 6.0+
- Build essentials (make, pkg-config)

**macOS**:
- Xcode Command Line Tools
- CMake installed via Homebrew or direct download

## Running Tests

### Quick Start

1. **Build libzmq first** (if not already built):

   **Windows**:
   ```powershell
   .\build-scripts\windows\build.ps1
   ```

   **Linux**:
   ```bash
   ./build-scripts/linux/build.sh
   ```

   **macOS**:
   ```bash
   ./build-scripts/macos/build.sh arm64  # or x86_64
   ```

2. **Run tests**:

   **Windows**:
   ```powershell
   cd tests
   .\run_tests.ps1 -Architecture x64
   ```

   **Linux/macOS**:
   ```bash
   cd tests
   ./run_tests.sh
   ```

### Manual Test Execution

If you prefer manual control:

1. **Configure tests**:
   ```bash
   cd tests
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   ```

2. **Build tests**:
   ```bash
   cmake --build . --config Release
   ```

3. **Run individual tests**:

   **Windows**:
   ```powershell
   $env:PATH = "..\..\dist\windows-x64;$env:PATH"
   .\Release\test_basic.exe
   .\Release\test_curve.exe
   ```

   **Linux**:
   ```bash
   export LD_LIBRARY_PATH=../../dist/linux-x64:$LD_LIBRARY_PATH
   ./test_basic
   ./test_curve
   ```

   **macOS**:
   ```bash
   export DYLD_LIBRARY_PATH=../../dist/macos-arm64:$DYLD_LIBRARY_PATH
   ./test_basic
   ./test_curve
   ```

## Test Output

### Successful Test Run

```
=== libzmq Test Suite ===

Platform: linux-x64
Library path: /path/to/dist/linux-x64

Configuring tests...
Building tests...
[OK] Tests built successfully

=== Running Tests ===

Running test_basic...
=== libzmq Basic Functionality Tests ===

[TEST] ZMQ Version: 4.3.5
[PASS] Version check
[PASS] Context creation
[PASS] Context termination
[PASS] Socket creation
[PASS] Socket close
[PASS] REP socket bind
[PASS] REQ socket connect
[PASS] Message send
[PASS] Message receive and verification
[PASS] REQ/REP pattern complete

=== Test Summary ===
All tests PASSED
[PASS] test_basic

Running test_curve...
=== libzmq CURVE Encryption Tests ===

[PASS] CURVE mechanism available
[PASS] CURVE keypair generation
       Public key (first 16 chars): xH9fK2L#mP3nQ7rT...
       Secret key (first 16 chars): yJ4gN8vBcD6sW1xA...
[PASS] CURVE key format validation
[PASS] CURVE server setup
[PASS] CURVE client setup
[PASS] Encrypted message send
[PASS] Encrypted message receive and decryption
[PASS] Complete CURVE encrypted communication

=== Test Summary ===
All tests PASSED

[SUCCESS] libsodium is properly statically linked
          CURVE encryption is fully functional
[PASS] test_curve

=== Test Summary ===
Tests passed: 2
Tests failed: 0

✓ All tests passed!

libzmq validation successful:
  ✓ Library loads correctly
  ✓ Basic socket operations work
  ✓ CURVE encryption is functional
  ✓ libsodium is statically linked
```

### Failed Test Run

If tests fail, the output will indicate which specific test failed and provide troubleshooting guidance:

```
[FAIL] CURVE mechanism not available
       libsodium may not be properly linked

=== Test Summary ===
1 test(s) FAILED

[ERROR] libsodium may not be properly linked

Troubleshooting:
  1. Verify libzmq was built correctly
  2. Check library dependencies:
     ldd ../../dist/linux-x64/libzmq.so
  3. Ensure libsodium is statically linked (no external libsodium dependency)
```

## Validation Checklist

Tests verify the following requirements:

- ✅ **Library Loading**: libzmq loads without missing dependencies
- ✅ **Version Compatibility**: libzmq version is 4.x.x or higher
- ✅ **Socket Operations**: Context and socket creation/destruction work
- ✅ **Message Passing**: REQ/REP pattern successfully exchanges messages
- ✅ **CURVE Support**: `zmq_has("curve")` returns true
- ✅ **Keypair Generation**: `zmq_curve_keypair()` succeeds
- ✅ **Encrypted Communication**: CURVE client-server handshake and message exchange work
- ✅ **Static Linking**: No external libsodium dependency (verified via ldd/otool/dumpbin)

## Troubleshooting

### Test Build Failures

**Issue**: CMake cannot find libzmq
```
[ERROR] libzmq not found in dist/linux-x64
```

**Solution**: Build libzmq first using platform-specific build scripts.

**Issue**: Missing zmq.h header
```
[WARNING] zmq.h not found. Tests may fail to compile.
```

**Solution**: The build scripts should install headers. Check `dist/<platform>/include` or build/libzmq/include directories.

### Test Execution Failures

**Issue**: Library not found at runtime
```
error while loading shared libraries: libzmq.so.5: cannot open shared object file
```

**Solution** (Linux):
```bash
export LD_LIBRARY_PATH=../../dist/linux-x64:$LD_LIBRARY_PATH
```

**Solution** (macOS):
```bash
export DYLD_LIBRARY_PATH=../../dist/macos-arm64:$DYLD_LIBRARY_PATH
```

**Solution** (Windows):
```powershell
$env:PATH = "..\..\dist\windows-x64;$env:PATH"
```

**Issue**: CURVE tests fail
```
[FAIL] CURVE mechanism not available
```

**Solution**: Verify libsodium is statically linked:

**Linux**:
```bash
ldd dist/linux-x64/libzmq.so | grep sodium
# Should show NO external libsodium dependency
```

**macOS**:
```bash
otool -L dist/macos-arm64/libzmq.dylib | grep sodium
# Should show NO external libsodium dependency
```

**Windows**:
```powershell
dumpbin /DEPENDENTS dist\windows-x64\libzmq.dll | findstr sodium
# Should show NO external libsodium dependency
```

### Platform-Specific Issues

**Windows**: Visual C++ Runtime Missing
```
[ERROR] The code execution cannot proceed because vcruntime140.dll was not found
```

**Solution**: Install Visual C++ Redistributable for Visual Studio 2019/2022.

**Linux**: GLIBC Version Mismatch
```
[ERROR] version `GLIBC_2.XX' not found
```

**Solution**: Build on a system with compatible GLIBC version, or use an older base system for builds.

**macOS**: Code Signing Issues
```
[ERROR] "libzmq.dylib" cannot be opened because the developer cannot be verified
```

**Solution**:
```bash
xattr -d com.apple.quarantine dist/macos-arm64/libzmq.dylib
```

## Integration with CI/CD

Tests are designed to integrate with GitHub Actions workflows. See `.github/workflows/` for automated test execution on:

- Push to main branch
- Pull requests
- Release tags

CI workflow automatically runs tests for all platforms and uploads test results as artifacts.

## Return Codes

Test programs use the following exit codes:

- `0`: All tests passed
- `1`: One or more tests failed

Test runners aggregate results and return:

- `0`: All test programs passed
- `1`: One or more test programs failed

## Contributing

When adding new tests:

1. Create a new `test_*.c` file in this directory
2. Add the test executable to `CMakeLists.txt`
3. Update test runners to execute the new test
4. Update this README with test description
5. Verify tests pass on all platforms before submitting PR

## License

Tests follow the same license as the project (MPLv2 for libzmq components).
