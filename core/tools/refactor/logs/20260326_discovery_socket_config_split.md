# 2026-03-26 discovery socket-config bounded slice

## 요약

- `discovery_bootstrap_runtime_t` 안에 다시 뭉쳐 있던 `routing-id lock`,
  socket option 저장/적용, TLS client option materialization 책임을
  `discovery_bootstrap_socket_config_t`로 분리했다.
- `bootstrap/uplink`의 기존 socket 생성 경로는 새 owner만 통해
  routing-id와 socket option을 적용하도록 좁혔다.
- discovery facade/public contract는 유지했고, bootstrap dealer/uplink
  lifecycle state는 이번 slice 범위 밖으로 남겨 두었다.

## 수정 파일

- `core/src/services/discovery/discovery.hpp`
- `core/src/services/discovery/discovery_runtime_internal.hpp`
- `core/src/services/discovery/discovery_bootstrap.cpp`
- `core/tests/unittest/unittest_typed_option.cpp`

## 검증

- `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- `cmake --build core/build -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -R '^(unittest_typed_option|test_spot_service_introspection|test_spot_service_introspection_monitors|test_spot_service_introspection_snapshots|test_spot_service_introspection_metadata_local)$' -j1`

결과:

- `unittest_typed_option` pass
- `test_spot_service_introspection` pass
- `test_spot_service_introspection_monitors` pass
- `test_spot_service_introspection_snapshots` pass
- `test_spot_service_introspection_metadata_local` pass

## 성능 해석

- 이번 slice는 `core/src/services/discovery/`의 bootstrap/control-path
  경계 정리이며 steady-state data path를 건드리지 않았다.
- 실행 가이드 `5.3`의 "서비스 local control path 정도의 변경이면 targeted
  integration + 기존 perf reasoning으로 충분할 수 있다"에 따라 full perf는
  이번 iteration에서 생략했다.

## 남은 범위

- `discovery_bootstrap_runtime_t`에 남은 bootstrap dealer state,
  bootstrap retry/wakeup, bootstrap success adoption과
  `discovery_uplink_runtime_t` 사이의 ownership 경계는 후속 slice에서
  계속 줄여야 한다.
