# 2026-03-26 discovery bootstrap/uplink state split

## 이번 bounded slice

- `discovery_bootstrap_runtime_t`가 bootstrap request state, timeout reset,
  bootstrap success commit을 직접 흡수하도록 정리했다.
- `discovery_uplink_runtime_t`는 bootstrap 성공 결과 전체를 해석하지 않고,
  uplink endpoint 기억과 report dealer 채택만 담당하도록 좁혔다.
- 기존 socket option/routing-id owner인
  `discovery_bootstrap_socket_config_t`는 bootstrap runtime의 공개된
  state helper만 사용하도록 줄여 bootstrap internals 직접 접근을 없앴다.

## change amplification 기록

```text
대표 시나리오: registry bootstrap 성공 후 uplink endpoint/report dealer 채택 규칙 변경
기존 수정 지점: discovery_bootstrap.cpp bootstrap success block + connect_registry loop + uplink remember_bootstrap_success
목표 수정 지점: discovery_bootstrap_runtime_t state helpers + discovery_uplink_runtime_t adoption helpers
줄인 중복: bootstrap request sent/timeout reset, bootstrap success commit, uplink endpoint/report dealer 채택 분기
```

## 검증

- `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- `cmake --build core/build -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -R '^(unittest_typed_option|test_spot_service_introspection|test_spot_service_introspection_monitors|test_spot_service_introspection_snapshots|test_spot_service_introspection_metadata_local)$' -j1`

검증 결과:

- build 성공
- `test_spot_service_introspection` pass
- `test_spot_service_introspection_monitors` pass
- `test_spot_service_introspection_snapshots` pass
- `test_spot_service_introspection_metadata_local` pass
- `unittest_typed_option` pass

## 메모

- 이 slice는 `core/src/services/discovery/` control/runtime path만 다뤘다.
- `core/src/services/discovery/` steady-state message path 변경은 없어서 실행
  가이드 `5.3` 기준 focused perf 없이 targeted discovery 회귀로 닫았다.
- final perf gate는 아직 마지막 종료 단계가 아니므로 실행하지 않았다.
