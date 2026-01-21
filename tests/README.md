# zlink Test Suite

Unity-based functional tests for zlink. Tests are built from the top-level
CMake build when `ZMQ_BUILD_TESTS=ON` is enabled.

## Quick Start

```bash
./build.sh
ctest --output-on-failure
```

## Manual Build

```bash
cmake -B build -DZMQ_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Notes

- Functional tests live in `tests/test_*.cpp`.
- Internal unit tests live in `unittests/unittest_*.cpp`.
- CURVE/libsodium and GSSAPI are not supported in zlink.
