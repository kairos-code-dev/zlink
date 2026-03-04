# C++ Porting Issue Report

## Scope
This report captures core-level instability found while validating `core/tests`
ports in `bindings/cpp/tests` on March 4, 2026.

## Issue 1: `service_discovery` intermittent timeout (core-level)

### Summary
- The same scenario times out in both stacks:
- `core/tests/discovery/test_service_discovery.cpp` (`test_service_discovery`)
- `bindings/cpp/tests/test_cpp_core_service_discovery.cpp`
  (`test_cpp_core_service_discovery`)
- This is not a C++-binding-only regression.

### Repro commands
- Core:
  - `ctest --test-dir core/build -j1 --output-on-failure -R '^test_service_discovery$'`
- C++ binding:
  - `ctest --test-dir bindings/cpp/build -j1 --output-on-failure -R '^test_cpp_core_service_discovery$'`
- Flake-frequency sample:
  - Core 5 runs: 3 timeout / 2 pass
  - C++ 5 runs: 4 timeout / 1 pass

### Observed evidence
- Core timeout is set to 10s (`core/tests/CMakeLists.txt`), C++ timeout is 120s
  (`bindings/cpp/CMakeLists.txt`), so C++ appears much slower when this flake
  occurs.
- In a failed core run, ctest output shows:
  - `test_discovery_provider_registration:PASS`
  - `test_discovery_service_filtering:PASS`
  - `test_discovery_heartbeat_timeout:PASS`
  - then process timeout before completion.
- This suggests hang in or after the final subtest path
  (`test_discovery_weight_update`) / teardown path.

### Cross-stack timing evidence (same 61 mapped tests)
- Core (`test_*`, mapped 61): timeout on `test_service_discovery`.
  - Real wall time observed: ~68s.
- C++ (`test_cpp_core_*`, mapped 61): timeout on
  `test_cpp_core_service_discovery`.
  - Real wall time observed: ~142s.
- The dominant delta is timeout budget (10s vs 120s), not only execution logic.

### Artifacts
- Compare run summary:
  - `tmp/compare_20260304_171600/summary.txt`
- Timeout capture logs:
  - `tmp/service_discovery_issue_20260304_172747/core_try_1.log`
  - `tmp/service_discovery_issue_20260304_172747/cpp_try_1.log`
  - `tmp/service_discovery_issue_20260304_172747/core_attempts.log`
  - `tmp/service_discovery_issue_20260304_172747/cpp_attempts.log`

## Request to core owner
- Please investigate `service_discovery` hang/termination path in core tests.
- Candidate area: final service-discovery update/cleanup path and background
  worker termination in discovery/registry/receiver shutdown sequence.
