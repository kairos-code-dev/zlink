# 2026-03-26 spot runtime state hub

## 이번 bounded slice

- `spot_runtime_t`가 control task id와 connected peer version을 직접
  보관하는 `spot_control_runtime_state_t`를 갖도록 바꿨다.
- `spot_node_control.cpp`는 더 이상 runtime 내부 bookkeeping 필드를 직접
  조작하지 않고, runtime helper로 control loop 수명과 peer-version 관찰을 위임한다.
- shutdown 경로도 `spot_node_lifecycle.cpp`에서 runtime helper만 호출하도록
  좁혀, control task lifecycle이 `spot_runtime` 내부 경계로 모이게 했다.

## change amplification 기록

```text
대표 시나리오: control task suspend/resume 규칙 또는 connected peer version 추적 규칙 변경
기존 수정 지점: spot_node_control.cpp ensure/wake/suspend path + refresh_connected_peer_endpoints + spot_node_lifecycle.cpp destroy + spot_node.hpp field
목표 수정 지점: spot_runtime_t control state helpers + spot_node_control.cpp policy call sites
줄인 중복: task_id 직접 소유, connected peer version bookkeeping, shutdown 시 task clear 분기
```

## 검증

- `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- `cmake --build core/build -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -R '^(unittest_spot_runtime_control_state|unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|unittest_spot_subject_access)$' -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_spot_service_introspection)$' -j1`
- `ctest --test-dir core/build --output-on-failure`
- `ctest --test-dir core/build --output-on-failure -R '^test_reconnect_ivl$' -j1`
- `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$' -j1`

검증 결과:

- build 성공
- 새 `unittest_spot_runtime_control_state` 포함 targeted unit tests pass
- `test_multi_socket_contract_regressions` pass
- `test_spot_service_introspection` pass
- 전체 `ctest` 1차에서 `test_reconnect_ivl` timeout 실패
- 단일 재현 `test_reconnect_ivl` pass
- 전체 `ctest` 2차에서 `test_multi_spot_benchmark_process` callback smoke 실패
- 단일 재현 `test_multi_spot_benchmark_process` pass

관련 로그:

- `doc/plan/refactor/3nd/logs/20260326_spot_runtime_state_hub_ctest_all.log`
- `doc/plan/refactor/3nd/logs/20260326_reconnect_ivl_single_repro.log`
- `doc/plan/refactor/3nd/logs/20260326_spot_runtime_state_hub_ctest_all_rerun.log`
- `doc/plan/refactor/3nd/logs/20260326_multi_spot_benchmark_process_single_repro.log`

## 메모

- 이 slice는 `core/src/services/spot/` control/runtime path만 다뤘다.
- steady-state send/recv 경로에는 새 indirection, heap allocation, lock을 넣지 않고
  control-state bookkeeping만 runtime owner 쪽으로 이동했다.
- 후속 최종 검증으로 전체 `ctest`, `./core/tests/run_test_lanes.sh --include-e2e`,
  execution gate, full perf gate가 모두 무실패로 끝났고 상세 근거는
  `doc/plan/refactor/3nd/logs/20260326_final_closeout.md`에 정리했다.
