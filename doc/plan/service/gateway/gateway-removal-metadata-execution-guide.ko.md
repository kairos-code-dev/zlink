# Gateway 삭제 / Metadata 후속 작업 Execution Guide

> 상태: active
> 대상 범위: `core/`, `core/tests/`, `core/perf/`, `bindings/`, `doc/plan/service/gateway/`
> 목적: `gateway` 삭제와 metadata 후속 작업을 랄프 루프에서 중단 없이 순서대로 끝내기 위한 실행 기준 고정

## 1. 문서 목적

이 문서는 `gateway` 관련 작업의 실행 authority다.
작업 순서, 종료 조건, 실행 체크리스트는 이 문서가 고정한다.
실제 구현 범위와 설계 intent는 아래 master plan 문서들이 함께 authority다.
새 설계를 제안하지 않는다.

설계 authority는 아래 문서들로 고정한다.

- [`gateway-removal-plan.ko.md`](./gateway-removal-plan.ko.md)
- [`socket-metadata-sharing-plan.ko.md`](./socket-metadata-sharing-plan.ko.md)
- [`README.ko.md`](./README.ko.md)

설계 판단이 흔들리면 먼저 authority 문서를 고치고 그 다음 코드를 수정한다.

## 2. 실행 authority

실행 authority:

- [`gateway-removal-metadata-execution-guide.ko.md`](./gateway-removal-metadata-execution-guide.ko.md)

master plan authority:

- [`gateway-removal-plan.ko.md`](./gateway-removal-plan.ko.md)
- [`socket-metadata-sharing-plan.ko.md`](./socket-metadata-sharing-plan.ko.md)

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 상세 authority 문서들이다.
- 자동 실행이 필요하면 [`run_gateway_removal_metadata_execution.sh`](./run_gateway_removal_metadata_execution.sh)를 사용한다.
- 이 스크립트는 내부적으로 공통 supervisor인
  [`core/tools/run_codex_execution_guide_loop.sh`](../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.

## 2.1 고정 작업 순서

1. `gateway` 삭제 범위와 migration 공백을 먼저 확정한다.
2. `gateway` public/internal/protocol/test/core-perf/doc/bindings 잔여물을 제거한다.
3. 삭제 직후 POSD 관점으로 관련 코드를 한 번 리팩토링한다.
4. 삭제 후에도 필요한 `value` / `metadata` / member query contract가 실제로 남는지 판정한다.
5. 실제 공백이 남을 때만 metadata 작업을 진행한다.
6. metadata 작업 완료 후 POSD 관점으로 다시 한 번 리팩토링한다.
7. 문서, 검증, 종료 판정을 정리한다.

기본값은 `gateway` 삭제로 끝내는 것이다.
metadata infra를 먼저 만들고 `gateway` 삭제를 나중에 결정하는 흐름으로 되돌리지 않는다.

## 2.2 Definition of Done

아래를 모두 만족해야 이번 묶음 작업이 끝난다.

- `gateway` family symbol과 전용 구현이 `core/`, `core/tests/`, `core/perf/`, `bindings/`에서 제거된다.
- 삭제 직후 관련 discovery/registry/service/perf 코드에 대해 POSD 리팩토링이 반영된다.
- metadata/member query contract는 삭제 후 실제 공백이 남을 때만 구현된다.
- metadata 작업을 진행했다면 완료 후 관련 코드에 대해 두 번째 POSD 리팩토링이 반영된다.
- execution guide와 세부 plan 문서 상태가 실제 코드 상태와 일치한다.

## 2.3 단계 매핑

| 실행 가이드 | authority 문서 | 의미 |
| --- | --- | --- |
| `5.1 authority / preflight 정리` | execution guide 2.1, removal plan 9 | 순서, 범위, migration 공백, 검증 baseline 고정 |
| `5.2 gateway 제거 구현` | removal plan 9 Phase 2, 11, 12 | core/core-tests/core-perf 기준 `gateway` 제거 |
| `5.3 삭제 직후 POSD 리팩토링` | removal plan 9 Phase 5 | 삭제 후 discovery/registry/service/perf 단순화 |
| `5.4 metadata 착수 판정` | removal plan 9 Phase 3, metadata plan 6.1 | 실제 공백이 남는지 판정하고 metadata 착수 여부 결정 |
| `5.5 metadata 모델 / plumbing` | metadata plan 7, 8, 9 Phase 1~3 | `value` / `metadata` / member row 모델과 internal plumbing |
| `5.6 metadata query / consumer 연결` | metadata plan 7, 9 Phase 4~5 | query surface와 consumer 연결 |
| `5.7 metadata 완료 후 POSD 리팩토링` | metadata plan 9 Phase 6 | metadata 도입 후 구조 단순화 |
| `5.8 문서 / 최종 검증` | removal plan 12, metadata plan 10~12 | 문서와 종료 판정 정렬 |

## 3. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- authority 문서만으로는 해결할 수 없는 C API/ABI 계약 충돌
- 사용자 변경과 직접 충돌하는 워크트리 변경 발견
- `core/`, `core/tests/`, `core/perf/`, `bindings/`, `doc/plan/service/gateway/`만으로 해결 불가능한 blocker

위 경우가 아니면:

1. 첫 미완료 항목을 잡는다.
2. 코드 변경과 회귀 검증을 같이 수행한다.
3. 관련 문서를 현재 상태에 맞게 갱신한다.
4. 해당 단계 범위만 묶어 commit 한다.
5. push 한다.
6. guide 상태를 갱신한다.
7. 다음 미완료 항목으로 넘어간다.

단계 완료 후 commit / push 없이 다음 단계로 넘어가지 않는다.

## 4. 기본 실행 명령

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1

./core/tests/run_test_lanes.sh --include-e2e
```

삭제 잔여물 확인 명령:

```bash
rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" \
  core core/tests core/perf bindings doc/plan/service/gateway \
  -g '!doc/plan/service/gateway/logs/**'
```

필수 git 명령:

```bash
git status --short
git add <관련 파일들>
git commit -m "<단계 목적을 드러내는 메시지>"
git push
```

## 5. 남은 작업 체크리스트

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 authority / preflight 정리

상태: `완료`

진행 메모:

- execution guide 검색 baseline이 `logs/` 산출물에 오염되지 않도록 source 문서만 보게 수정했다.
- removal plan에 있던 `bindings/` 제거 범위와 최종 grep 범위를 execution guide에 반영했다.
- execution guide의 authority 설명을 `실행 가이드 + master plan` 조합으로 정렬했다.
- 현재 baseline 기준 잔여물은 `core/include 62`, `core/src 219`, `core/tests 145`, `core/perf 36`, `bindings 278`건이다.
- 검증: `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- 검증: `cmake --build core/build -j"$(nproc)" --target unittest_service_mode_policy unittest_typed_option`
- 검증: `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|unittest_typed_option)$'`

작업:

- execution guide와 상세 plan 사이의 순서/범위 불일치를 먼저 정리
- `gateway` 삭제 대상과 metadata 후속 대상이 충돌 없이 분리됐는지 확인
- `core/tests`, `core/perf`, `bindings`, source 문서 기준의 삭제 잔여물 검색 baseline을 남긴다

완료 기준:

- 어떤 작업을 먼저 하고 무엇을 나중에 하는지 문서로 더 이상 흔들리지 않는다
- `gateway` 삭제와 metadata 후속 작업의 경계가 설명 가능하다

검증:

- `rg -n "zlink_gateway_|ZLINK_GATEWAY_|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf bindings doc/plan/service/gateway -g '!doc/plan/service/gateway/logs/**'`

### 5.2 gateway 제거 구현

상태: `완료`

진행 메모:

- `core/tests/unittest/unittest_service_mode_policy.cpp`에서 gateway callback policy 회귀를 제거했다.
- `core/tests/unittest/unittest_typed_option.cpp`에서 gateway/discovery typed option 회귀를 제거했다.
- `core/tests/testutil_unity.hpp`에서 gateway send/recv test helper alias를 제거했다.
- `core/tests/e2e/discovery/test_gateway.cpp`를 삭제했다.
- `core/tests/integration/discovery/test_gateway_handover.cpp`를 삭제했다.
- `core/tests/integration/discovery/test_gateway_with_handler.cpp`를 삭제했다.
- `core/tests/integration/monitoring/test_gateway_monitor_process.cpp`를 삭제했다.
- `core/tests/CMakeLists.txt`에서 gateway 전용 test target을 lane 구성에서 제외했다.
- `core/tests/README.md`의 삭제된 gateway umbrella/lane 설명을 현재 상태에 맞게 정리했다.
- `core/include/zlink.h`, `core/src/api/`, `core/src/services/discovery/`, `core/perf/`에서 gateway public/internal/protocol/perf 경로를 제거했다.
- 공통 routing-id helper를 `core/src/services/discovery/routing_id_utils.hpp`로 승격해 discovery/spot에서 gateway 없이 재사용하도록 정리했다.
- `bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`, `bindings/python`에서 gateway public wrapper, enum, native glue, 전용 테스트를 제거하고 `bindings/perf/run_policy_bench.py`의 gateway 벤치 엔트리를 삭제했다.
- `bindings/cpp/API_DRAFT.md`, `bindings/java/PORTING_ISSUES.md`, `bindings/dotnet/perf/single/Zlink.BindingBench/common/PerfCommon.cs`에서 삭제된 gateway API/테스트/helper 잔재를 정리했다.
- `core/tests/run_thread_safe_contract_*.sh`, `core/tests/README.md`에서 gateway stress/tsan/scaling lane 설명과 엔트리를 제거했다.
- `core/perf/run_benchmarks*.{sh,ps1}`, `core/perf/multi/common/perf_common.hpp`, `core/perf/README*.md`, `core/perf/single/tests/*.py`에서 gateway 패턴과 기대값을 제거했다.
- `bindings/dotnet/perf`, `bindings/java/perf`, `bindings/perf/run_binding_multi.sh`에서 gateway perf main entry, multi runner entry, TLS helper, 전용 소스 파일을 제거했다.
- `bindings/node/tests/helpers.js`, `bindings/python/tests/integration/helpers.py`, `bindings/dotnet/tests/Zlink.Tests/CoreTestSupport.cs`에서 삭제된 gateway 테스트 helper를 제거했다.
- 현재 exact symbol baseline 기준 `core/`, `core/tests/`, `core/perf/`, `bindings/`에서는 `zlink_gateway_*`, `ZLINK_GATEWAY_*`, `SERVICE_TYPE_GATEWAY`, `SERVICE_ROLE_GATEWAY`, `gateway_peer`, `perf_gateway`, `comp_src_gateway` 잔여물이 없다.
- 현재 broad 잔여 hotspot은 bindings perf 구현 계획 문서(`CPP_PORTING_PLAN.md`, `DOTNOET_IMPLEMENTATION_PLAN.md`, `JAVA_IMPLEMENTATION_PLAN.md`) 중심의 historical/migration 서술이다.
- `spot` callback sub destroy가 마지막 filtered sub 제거 뒤에도 불필요한 `replay_subscriptions` control command를 보내던 경로를 정리했다.
- 단일 재현 기준 `PERF_RECV_MODE=callback PERF_SINGLE_DURATION_SECONDS=1 PERF_SINGLE_WARMUP_SECONDS=1 ./core/build/bin/perf_spot current tcp 64 >/tmp/perf_spot_fixcheck.out`가 종료까지 정상 통과한다.
- 검증: `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- 검증: `cmake --build core/build -j"$(nproc)" --target unittest_service_mode_policy unittest_typed_option`
- 검증: `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|unittest_typed_option)$'`
- 검증: `cmake --build core/build -j"$(nproc)"`
- 검증: `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
- 검증: `ctest --test-dir core/build --output-on-failure -R '^(test_single_spot_benchmark_process|unittest_service_mode_policy|unittest_typed_option)$' -j1`
- 검증: `python -m pytest core/perf/single/tests/test_run_comparison_policy.py core/perf/single/tests/test_multi_run_comparison_policy.py`
- 검증: `rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf bindings -g '!**/build/**' -g '!**/obj/**' -g '!**/bin/**'`

작업:

- `gateway-removal-plan.ko.md`의 삭제 범위에 맞춰 `core/`, `core/tests/`, `core/perf/`, `bindings/`에서 `gateway` 제거
- `core/include/zlink.h`의 `gateway` public function/type/constant와
  registry snapshot/count의 `gateway_peer_entry_count` 제거
- public/internal/protocol/test/perf 잔여물 제거
- `service_discovery_api.cpp`의 gateway service-type special case와
  discovery/registry query path의 gateway summary/store/message branch 제거
- bindings source tree의 API draft / porting note / perf implementation plan에 남은
  gateway 서술이 실제 코드 상태와 어긋나면 먼저 문서를 현재 상태로 갱신
- 필요하면 migration 문서/메모와 bindings 대응 메모를 authority 문서에 반영

완료 기준:

- `gateway-removal-plan.ko.md`의 제거 완료 판정과 최종 검증 항목을 만족한다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -L integration -j1`
- `rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf bindings doc/plan/service/gateway -g '!doc/plan/service/gateway/logs/**'`

### 5.3 삭제 직후 POSD 리팩토링

상태: `완료`

진행 메모:

- discovery bootstrap/uplink dealer 생성/종료와 control task wakeup 경로를 `discovery_t` helper로 모아 socket lifecycle 책임을 한곳으로 줄였다.
- `spot_node_t`의 peer/readiness/replay bookkeeping을 `spot_peer_state_t`로 모아 hidden coupling을 줄이고 읽기 경계를 정리했다.
- `spot` monitor close는 외부 스레드의 in-flight callback에서도 동기 close 경로로 정리해 monitor userdata 수명과 close 결과가 어긋나지 않도록 맞췄다.
- `spot` control loop에서 누락됐던 `emit_pending_pub_delivery_ready_events()` 호출을 복구해 pub ready 신호가 control tick에서 drain되도록 맞췄다.
- `core/tests/run_thread_safe_contract_stress.sh`, `core/tests/run_thread_safe_contract_tsan.sh`, `core/tests/README.md`를 현재 CTest 등록 상태에 맞춰 정렬해 gate wrapper가 시작 단계에서 멈추지 않도록 수정했다.
- `./core/tools/run_execution_gate_loop.sh --label gateway_removal_metadata_gate --count 1` 최소 gate는 현재 thread-safe stress lane을 끝까지 통과했고 증거 로그는 `doc/plan/refactor/2nd/logs/gateway_removal_metadata_gate_20260326_061636.log`에 남겼다.
- `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$' -j1` 단일 재현은 2026-03-26에 78.81초로 통과했다.
- `./core/tests/run_test_lanes.sh --include-e2e` 전체 lane 재실행도 2026-03-26에 끝까지 통과했다.

작업:

- `gateway` 제거 뒤 남은 discovery/registry/service/perf 코드에서 hidden coupling 정리
- shallow wrapper, 임시 adapter, 이름만 generic한 helper 제거 또는 통합
- 삭제 plan 문서의 POSD 단계와 검증 메모를 갱신

완료 기준:

- 삭제 후 관련 코드가 POSD 기준으로 더 짧게 설명 가능하다
- `gateway` 제거 때문에 남겨 둔 우회 경로가 사라진다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$' -j1`
- `./core/tests/run_test_lanes.sh --include-e2e`

### 5.4 metadata 착수 판정

상태: `완료`

진행 메모:

- `core/include/zlink.h`, `core/src/api/`, `core/tests/`, `bindings/` 기준으로 `zlink_discovery_set_value`, `zlink_discovery_set_metadata`, `zlink_registry_member_peers`, `zlink_discovery_member_peers`, `zlink_member_peer_entry_t` 같은 generic metadata/member query surface는 아직 없다.
- discovery 내부에는 `provider_info_t.weight`, `register_service(..., uint32_t weight_)`, `update_service_weight(..., uint32_t weight_)`와 registry register/update-weight plumbing이 남아 있어 numeric attribute 자체는 internal 전용 contract로만 존재한다.
- migration guide만으로는 이 공백을 메울 수 없고 `gateway` 삭제 뒤에도 "remote service peer attribute를 generic하게 읽고 배포하는 최소 contract"가 실제로 비어 있으므로 metadata 작업을 계속 진행한다.

작업:

- `gateway-removal-plan.ko.md`의 Phase 3 기준으로 삭제 후 실제 공백을 다시 판정
- migration guide만으로 끝낼 수 있는지 먼저 결정
- 실제 공백이 남는 경우에만 metadata 작업을 다음 단계로 연다

완료 기준:

- metadata 작업이 정말 필요한지 여부가 문서와 코드 기준으로 설명 가능하다
- 불필요하면 여기서 metadata 단계를 종료하고 바로 `5.8`로 넘어갈 수 있다

검증:

- `rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf bindings doc/plan/service/gateway -g '!doc/plan/service/gateway/logs/**'`
- 관련 migration 메모 또는 authority 문서 상태 갱신

### 5.5 metadata 모델 / plumbing

상태: `완료`

진행 메모:

- `core/include/zlink.h`와 `core/src/api/`에 `zlink_discovery_set/get_value`, `zlink_discovery_set/get_metadata`, `zlink_member_peer_entry_t`, `zlink_registry_member_peers`, `zlink_registry_member_peer_metadata`, `zlink_discovery_member_peers`, `zlink_discovery_member_peer_metadata`를 추가했다.
- metadata maximum size는 discovery handle 공통 option `ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE`로 runtime-configurable 하게 열고 oversize set은 `EMSGSIZE`로 fail-fast 하도록 고정했다.
- discovery local owner는 `_local_value`, `_local_metadata`, `_metadata_max_size`를 보관하고 등록/갱신 시 registry uplink로 `value + metadata`를 함께 보낸다.
- registry/discovery service list propagation과 peer cache는 기존 `weight` 전용 frame 대신 `int64_t value + metadata blob`을 같이 싣도록 확장했다.
- registry canonical row와 discovery peer cache row는 모두 `routing_id + value`를 유지하고 full metadata는 blob query로 분리했다.

작업:

- `5.4`에서 실제 공백이 남는다고 판정된 경우에만
  `socket-metadata-sharing-plan.ko.md`의 Phase 1~3 진행
- `value`, `metadata`, member peer row, ownership, size limit, internal propagation path 구현
- 삭제 이후 공백을 메우는 최소 surface만 유지

완료 기준:

- metadata 모델과 internal plumbing이 authority 문서와 맞는다
- `gateway` 구조를 generic 이름으로 옮겨 적지 않는다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|unittest_typed_option|test_spot_service_introspection_metadata_local)$' -j1`

### 5.6 metadata query / consumer 연결

상태: `완료`

진행 메모:

- registry/discovery query surface가 실제로 `value` row와 metadata blob query를 반환하도록 연결했다.
- `core/src/services/spot/spot_node_control.cpp`와 `core/src/services/discovery/socket_discovery_attachment.cpp`는 topology refresh를 provider snapshot 경계에 남기고 `member_peers` surface는 policy/attribute query 경계로만 유지하도록 정리했다.
- `core/tests/e2e/spot/test_spot_service_introspection.cpp`에 local contract 회귀와 registry/discovery member peer query 회귀를 추가했고 새 query surface 위에서 discovery-managed pub/sub 왕복도 같이 검증한다.
- 검증: `ctest --test-dir core/build --output-on-failure -R '^(test_spot_service_introspection_metadata_local|test_spot_service_introspection_member_peers|test_spot_service_introspection)$' -j1`

작업:

- metadata plan의 query surface와 policy consumer 연결 진행
- metadata 작업을 실제로 시작한 경우에만 진행
- member peer query와 topology/introspection query 경계를 유지
- 필요하면 `core/tests` 회귀 추가

완료 기준:

- query surface가 동작하고 consumer가 이를 사용한다
- 운영 query와 정책 query의 역할이 다시 섞이지 않는다

검증:

- `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
- `ctest --test-dir core/build --output-on-failure -L integration -j1`

### 5.7 metadata 완료 후 POSD 리팩토링

상태: `완료`

진행 메모:

- discovery가 remote member row 계산과 local member 제외 규칙을 `snapshot_member_peers()` 안으로 모아 public query 구현 중복을 줄였다.
- metadata query surface를 붙인 뒤 topology refresh와 attribute query 책임을 다시 분리해 `spot_node_control`과 `socket_discovery_attachment`는 provider snapshot 경계를 유지하고 `member_peers`는 정책/attribute query 전용 surface로 남겼다.
- full lane 재실행 중 드러난 `test_spot_pubsub_scenario_recv_service_isolation` 회귀는 위 경계 복원으로 수정했고 단일 재현과 전체 lane에서 모두 사라졌다.
- `./core/tools/run_execution_gate_loop.sh --label gateway_removal_metadata_gate --count 1`는 2026-03-26 07:03:36 +0900 시작, 2026-03-26 07:04:32 +0900 종료로 success였고 stress log는 `doc/plan/refactor/2nd/logs/gateway_removal_metadata_gate_20260326_070336.log`에 남겼다.
- `./core/tests/run_test_lanes.sh --include-e2e` 전체 lane 재실행도 2026-03-26에 끝까지 통과했다.

작업:

- metadata 작업 완료 뒤 registry/discovery/service 코드를 다시 POSD 기준으로 정리
- 중복 query helper, compatibility adapter, shallow wrapper 제거 또는 통합
- metadata plan의 POSD 단계와 리스크/열린 질문 상태 갱신

완료 기준:

- metadata 도입 뒤에도 구조가 시간 순서가 아니라 추상 경계 기준으로 설명된다
- generic query surface가 새 허브나 shallow wrapper를 만들지 않는다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `./core/tests/run_test_lanes.sh --include-e2e`

### 5.8 문서 / 최종 검증

상태: `완료`

진행 메모:

- `README.ko.md`, `gateway-removal-plan.ko.md`, `socket-metadata-sharing-plan.ko.md`, execution guide를 실제 구현/authority 기준으로 정렬했다.
- master plan 문서 상태를 `completed`로 올리고 metadata query와 topology/provider 경계가 다시 섞이지 않도록 구현 결과를 문서에 남겼다.
- 최종 grep 기준 source 쪽 `gateway` 구현 잔여물은 없고 남은 검색 hit는 execution/master plan의 historical checklist 설명뿐이다.
- 검증: `cmake --build core/build -j"$(nproc)"`
- 검증: `./core/tests/run_test_lanes.sh --include-e2e`
- 검증: `./core/tools/run_execution_gate_loop.sh --label gateway_removal_metadata_gate --count 1`
- 검증: `rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf bindings doc/plan/service/gateway -g '!doc/plan/service/gateway/logs/**'`

작업:

- README, execution guide, 두 상세 plan의 상태와 완료 판정 정리
- 필요하면 `core/perf`, `bindings`, 테스트 문서의 잔여 gateway 언급 정리
- 종료 증거와 commit hash를 문서에 남길 수 있으면 남긴다

완료 기준:

- execution guide 기준으로 더 이상 미적용 사항이 없다

검증:

- `cmake --build core/build -j"$(nproc)"`
- `./core/tests/run_test_lanes.sh --include-e2e`
- `rg -n "zlink_gateway_|ZLINK_GATEWAY_|SERVICE_TYPE_GATEWAY|SERVICE_ROLE_GATEWAY|gateway_peer|perf_gateway|comp_src_gateway" core core/tests core/perf bindings doc/plan/service/gateway -g '!doc/plan/service/gateway/logs/**'`

## 6. 랄프 루프 종료 계약

아래 세 문장 외의 변형을 쓰지 않는다.

- 모든 미적용 사항이 끝났을 때만 정확히 `미적용 사항이 없습니다.` 출력
- 사용자 결정이 꼭 필요할 때만 정확히 `사용자 입력 필요: <한 줄 이유>` 출력
- 그 외에는 정확히 `계속 진행 필요` 출력
