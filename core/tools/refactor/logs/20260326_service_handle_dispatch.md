# 2026-03-26 Service Handle Dispatch Slice

## Scope

- `core/src/api/service_api.cpp`
- `core/src/api/service_api_internal.hpp`
- `core/src/api/service_option_api.cpp`
- `core/src/api/monitor_service_open_api.cpp`
- `core/src/api/service_poller_api.cpp`
- `core/src/api/service_handler_api.cpp`
- `core/src/api/service_spot_api.cpp`
- `core/tests/unittest/unittest_typed_option.cpp`

## Intent

Reduce repeated service-handle ownership branching in the service API layer by
introducing one internal handle-resolution helper and reusing it across option,
monitor, poller, and handler entry points.

## Verification

- `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- `cmake --build core/build -j"$(nproc)" --target unittest_typed_option unittest_service_mode_policy unittest_poller`
- `ctest --test-dir core/build --output-on-failure -R "unittest_(typed_option|service_mode_policy|poller)"`
- `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`

## Result

- All targeted service API regressions passed.
- Full `unittest` label passed: 14/14.
- Perf gate skipped intentionally: this slice only touched `core/src/api/`
  control-path dispatch and did not modify the hot-path directories listed in
  guide section 5.2.
