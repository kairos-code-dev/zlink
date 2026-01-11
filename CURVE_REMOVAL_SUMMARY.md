# CURVE/libsodium Removal Summary

## Changes Made

### 1. CMakeLists.txt (Lines 255-287)

**Removed:**
- `option(WITH_LIBSODIUM "Use libsodium" OFF)`
- `option(WITH_LIBSODIUM_STATIC "Use static libsodium library" OFF)`
- `option(ENABLE_LIBSODIUM_RANDOMBYTES_CLOSE "..." ON)`
- `option(ENABLE_CURVE "Enable CURVE security" OFF)`
- Complete `if(ENABLE_CURVE)` block with libsodium detection and configuration

**Replaced with:**
```cmake
# zlink: CURVE security permanently disabled (libsodium removed)
```

### 2. CMakeLists.txt (Lines 1453-1459, original)

**Removed:**
```cmake
if(SODIUM_FOUND)
  target_link_libraries(libzmq ${SODIUM_LIBRARIES})
  # On Solaris, libsodium depends on libssp
  if(${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
    target_link_libraries(libzmq ssp)
  endif()
endif()
```

### 3. CMakeLists.txt (Lines 1503-1509, original)

**Removed:**
```cmake
if(SODIUM_FOUND)
  target_link_libraries(libzmq-static ${SODIUM_LIBRARIES})
  # On Solaris, libsodium depends on libssp
  if(${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
    target_link_libraries(libzmq-static ssp)
  endif()
endif()
```

## Build Script Verification

### Linux (build-scripts/linux/build.sh)
✅ Parameters: `ARCH RUN_TESTS`
✅ VERSION file reading implemented
✅ CMake flags: `-DENABLE_CURVE=OFF -DWITH_LIBSODIUM=OFF`
✅ No libsodium dependencies
✅ Syntax: Valid

### macOS (build-scripts/macos/build.sh)
✅ Parameters: `ARCH RUN_TESTS`
✅ VERSION file reading implemented
✅ CMake flags: `-DENABLE_CURVE=OFF -DWITH_LIBSODIUM=OFF`
✅ No libsodium dependencies
✅ Syntax: Valid

### Windows (build-scripts/windows/build.ps1)
✅ Parameters: `-Architecture -RunTests`
✅ VERSION file reading implemented
✅ CMake flags: `-DENABLE_CURVE=OFF -DWITH_LIBSODIUM=OFF`
✅ No vcpkg dependencies for libsodium
✅ Syntax: Valid (PowerShell loads correctly)

## Remaining References

Only commented-out references remain in CMakeLists.txt:
- Line 255: Comment explaining CURVE removal
- Lines 1531-1532: Commented-out perf-tools libsodium linking (already disabled)

## Verification

CMake configuration tested successfully with no CURVE/libsodium errors:
```bash
cmake ../.. -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DBUILD_TESTS=OFF
# No errors related to CURVE or sodium
```

## Impact

- **Build size:** Reduced (no libsodium dependency)
- **Security:** CURVE encryption permanently disabled
- **Dependencies:** Simplified (no external crypto library needed)
- **Compatibility:** All platforms simplified to minimal build
- **Tests:** All existing tests should pass (CURVE-specific tests already removed)

## Next Steps

1. Test builds on all platforms (Linux x64/arm64, macOS x86_64/arm64, Windows x64/arm64)
2. Verify test suite runs successfully
3. Update documentation to reflect CURVE removal
4. Consider version bump if this is a breaking change
