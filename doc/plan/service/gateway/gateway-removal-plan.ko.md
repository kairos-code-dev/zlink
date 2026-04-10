# Gateway 제거 상세 계획

> 상태: completed
> 대상 범위: `core/`, `core/tests/`, `core/perf/`, `doc/plan/service/`, `doc/plan/discovery/`
> 목적: `gateway`를 먼저 제거하고, 남는 요구를 raw socket profile과 generic metadata/query contract로 재정리한다.

## 진행 상태

- Phase 1~5 구현이 `core/`, `core/tests/`, `core/perf/`, `bindings/`,
  `doc/plan/service/gateway/`에 반영됐다.
- `gateway` family public/internal/protocol/test/perf/bindings surface는 제거됐고,
  broad search 기준 남은 `gateway` 언급은 plan/migration 문서 설명만 남는다.
- 후속 generic contract는 registry/discovery metadata distribution으로만 다시 도입했고,
  topology/introspection query와 member peer attribute query 경계를 분리한 상태로 마감했다.
- 삭제 이후 POSD 정리에서는 discovery lifecycle helper, `spot_peer_state_t`,
  topology/provider 경계 정리를 통해 hidden coupling과 shallow wrapper를 줄였다.
- 검증: `./core/tests/run_test_lanes.sh --include-e2e`
- 검증: `./core/tools/run_execution_gate_loop.sh --label gateway_removal_metadata_gate --count 1`

## 0. 선행 결정

이 문서는 더 이상 "`gateway`를 유지할지 재평가"하는 문서가 아니다.
이번 작업 묶음에서는 아래 결정을 선행으로 고정한다.

- `gateway` family는 먼저 삭제한다.
- 삭제 작업은 public API, internal runtime, discovery/registry protocol,
  테스트, `core/perf`, 문서, bindings까지 함께 정리하는 것을 의미한다.
- 삭제 후에도 필요한 요구만 별도 공통 contract로 다시 도입한다.

즉 판단 순서는 "metadata infra를 먼저 만들고 삭제 여부를 나중에 결정"이 아니라
"먼저 `gateway`를 제거해서 개념 수를 줄이고, 꼭 필요한 공통 기능만 다시 설계"다.

## 1. 문제 정의

현재 `gateway`는 사용자에게 아래 두 가지 가치를 제공한다.

- raw `dealer/router`, `router/router` 조합보다 조금 단순한 facade
- registry/discovery를 이용한 metadata-aware routing 기반 send path

하지만 discovery 기반 raw socket service mode가 확장되면
`gateway`의 차별점은 약해질 수 있다.

특히 아래 의문이 생긴다.

- `gateway`가 raw socket topology를 감싼 얕은 wrapper에 가까운가
- 아니면 raw socket만으로는 설명하기 어려운 deep abstraction인가
- weighted routing의 본질이 `gateway` 자체가 아니라
  registry/discovery를 통한 service peer `value` / `metadata`
  distribution이라면,
  이를 공통 인프라로 끌어올리는 편이 더 단순한가

## 2. 현재 판단

현재까지의 판단은 아래와 같다.

- `gateway`의 편의성 차이는 존재하지만, raw `dealer/router` 조합으로 대체할 때
  사용자가 감수하는 비용은 "몇 줄 더 작성" 수준일 가능성이 크다.
- `gateway`의 더 본질적인 차이는 metadata-aware routing과 metadata distribution이다.
- 이 기능이 registry/discovery의 일반 메커니즘으로 승격 가능하다면,
  `gateway`는 장기적으로 별도 family가 아니라 raw socket service profile로 흡수될 수 있다.

즉 후속 검토의 핵심은 "gateway를 유지할지"가 아니라
"`gateway`가 품고 있던 요구 중 무엇을 generic contract로 되살려야 하는지"다.

## 3. 설계 목표

후속 설계가 만족해야 하는 목표는 아래와 같다.

- `gateway`가 제공하는 고유 contract를 분리해서 설명할 수 있어야 한다.
- raw socket profile로 대체 가능한 부분과 아닌 부분을 분리해야 한다.
- metadata-aware routing의 실질적 요구가 metadata distribution인지,
  `gateway` 전용 runtime contract인지 구분해야 한다.
- `gateway` 삭제 이유와 후속 대체 방향이 2-3문장 안에 설명 가능해야 한다.
- `gateway` 제거 시 관련 dead code와 우회 경로를 함께 삭제해
  주변 service/discovery/registry 코드를 더 단순하게 만들어야 한다.

## 4. 비목표

이번 계획은 아래를 바로 구현 대상으로 두지 않는다.

- `gateway` 동등 기능을 한 번에 모두 대체 구현하는 것
- raw socket profile에 weighted routing을 곧바로 도입하는 것
- 삭제 결정을 되돌려 `gateway` 존치안을 다시 여는 것

## 5. 핵심 가설

### 5.1 `gateway`의 핵심 가치는 metadata-aware routing이다

`gateway`의 실질적 차별점이
"raw socket 몇 개를 대신 열어준다"가 아니라
"registry/discovery를 통해 각 service peer의 `value` / `metadata`를 모으고,
send path가 그 정보를 사용해 routing policy를 수행한다"라면,
`gateway`를 평가할 때 핵심은 topology가 아니라 metadata-aware routing contract다.

현재 `gateway` public API는 `peer weight` 용어를 사용하지만,
후속 공통 모델은 generic `value + metadata`를 기준으로 정렬한다.
즉 `gateway`는 현재 `weight`를 소비하는 family이고,
후속 metadata layer는 이를 더 일반적인 numeric/blob attribute 모델로 끌어올리는 방향이다.

### 5.2 metadata distribution은 별도 공통 모듈로 분리 가능하다

metadata-aware routing에 필요한 정보가
`service_type + service_name + role + endpoint`에 부가된
`value + metadata`라면,
그 distribution은 registry/discovery 공통 인프라로 승격 가능하다.

이 경우 핵심은 `gateway`를 남기는 것이 아니라,
삭제 후에도 꼭 필요한 요구만 공통 metadata/member contract로 다시 도입하는 것이다.

## 6. 삭제 우선 기준

이번 계획은 `gateway` 존치 근거를 더 수집하지 않는다.
대신 삭제를 먼저 진행하는 이유를 아래처럼 고정한다.

- `gateway`는 raw socket topology 위에 얕게 덧씌운 family일 가능성이 높다.
- metadata-aware routing의 deep module 후보는 `gateway`가 아니라
  registry/discovery metadata distribution이다.
- 삭제를 먼저 해야 public/internal/protocol 전반의 hidden coupling을 드러내고
  어떤 contract가 진짜 필요한지 분리할 수 있다.
- 공통 기능이 실제로 필요하면 제거 이후 더 작은 surface로 재도입할 수 있다.

## 7. 목표 아키텍처 방향

장기 방향은 `gateway` 없는 구조로 고정한다.

1. `gateway` 삭제
   - public/internal/protocol/test/doc/bindings에서 `gateway` family 제거
2. generic contract 재도입
   - raw socket profile + registry/discovery metadata 공유로
     실제 필요한 요구만 더 작은 surface로 재구성

## 8. raw socket profile과의 관계

`gateway`가 흡수되는 방향이라면,
장기적으로는 아래 profile들이 후보가 된다.

- raw `router/router` metadata-aware routing profile
- raw `dealer/router` metadata-aware routing profile

이때 discovery는 연결 그래프를 만들고,
routing policy는 metadata를 읽어 local send path에서 적용한다.

즉 역할을 아래처럼 나눈다.

- discovery: service peer 연결/관측
- registry/discovery metadata layer: `value + metadata` distribution
- raw socket profile: routing policy consume

여기서 routing attribute query와 운영/관측 query는 분리한다.

- member peer query: routing policy가 소비하는 `value + metadata` snapshot
- topology/introspection query: 연결 상태와 운영 관측 정보

## 9. 단계 제안

### Phase 1. 삭제 범위 고정

- 현재 `gateway`가 제공하던 public/internal/protocol/test/doc/bindings surface를 확정
- topology convenience와 metadata-aware routing 요구를 분리해서 기록
- 삭제 후 반드시 남겨야 할 최소 사용자 시나리오를 식별

### Phase 2. `gateway` source tree 제거

- `gateway` 전용 public/internal symbol 삭제
- registry/discovery/service 계층에 남은 `gateway` 분기 제거
- protocol/store/query path의 `gateway` 특수 처리를 제거
- 테스트/`core/perf`/문서/bindings에서 `gateway` family를 삭제하거나 대체 경로로 갱신

### Phase 3. 삭제 후 공백 판정

- 삭제로 비게 된 사용자 시나리오를 raw socket/service contract 기준으로 다시 분류
- 추가 구현 없이도 끝낼 수 있는지 먼저 판정한다
- truly needed 요구만 generic metadata/member query surface 후보로 추린다
- migration guide만으로 충분한 항목과 실제 새 contract가 필요한 항목을 구분한다

### Phase 4. generic contract 후속 설계

- Phase 3 판정 결과 실제 공백이 남을 때만 진행한다
- registry/discovery metadata 공유 모델의 최소 범위를 설계
- member peer row와 topology/introspection query 분리 원칙 고정
- raw profile prototype 또는 migration 계획을 후속 문서로 넘긴다

### Phase 5. POSD 관점 후속 리팩토링

- `gateway` 삭제 뒤 남은 discovery/registry/service/perf 코드에서
  얕은 wrapper, 중복 adapter, hidden coupling을 다시 점검한다
- John Ousterhout의 POSD 기준으로
  deep module 경계를 더 분명하게 만들고 information hiding을 강화한다
- `gateway` 때문에 남겨 둔 우회 경로, 임시 branch, 이름만 generic한 helper를 정리한다
- lifecycle, topology query, routing attribute query의 책임이
  시간 순서가 아니라 추상 경계 기준으로 설명되도록 다시 다듬는다
- local convenience보다 전체 개념 수 감소와 change amplification 축소를
  우선하는 방향으로 주변 코드를 단순화한다

## 10. 제거 시 함께 정리할 C API 목록

`gateway`를 실제로 제거하는 방향으로 가면,
아래 public C API도 같이 제거 또는 대체 경로 정리가 필요하다.

### 10.1 생성 / 종료 / service ownership

- `zlink_gateway_new(...)`
- `zlink_gateway_destroy(...)`
- `zlink_gateway_attach_discovery(...)`

### 10.2 endpoint / peer 관리

- `zlink_gateway_bind(...)`
- `zlink_gateway_connect(...)`
- `zlink_gateway_disconnect(...)`

### 10.3 gateway 전용 routing / option surface

- `zlink_gateway_set_lb_strategy(...)`
- `zlink_gateway_update_peer_weight(...)`

### 10.4 monitor / introspection / snapshot surface

- `zlink_gateway_status_snapshot(...)`

### 10.5 registry query / introspection surface

- `zlink_registry_gateway_peers_snapshot(...)`
- `zlink_registry_gateway_peers_query(...)`
- `zlink_registry_query_gateway_peers_snapshot(...)`

### 10.6 event / enum / struct

아래 public type/constant도 함께 정리해야 한다.

- `zlink_gateway_lb_strategy_t`
- `ZLINK_GATEWAY_LB_ROUND_ROBIN`
- `ZLINK_GATEWAY_LB_WEIGHTED`
- `zlink_registry_gateway_peer_entry_t`
- `zlink_registry_gateway_peer_filter_t`
- `zlink_gateway_monitor_event_mask_t`
- `zlink_gateway_state_t`
- `zlink_gateway_status_t`
- `ZLINK_GATEWAY_*` monitor event / state constant

### 10.7 문서상 제거 기준

`gateway` 제거 시 C API 정리는 아래 기준으로 판정한다.

- `core/include/zlink.h`에서 `gateway` 전용 public symbol이 사라진다
- generic raw socket/service API로 대체 가능한 것은 migration 문서에 대응 관계를 남긴다
- routing attribute query는 member peer query로, 운영/관측은 topology/introspection query로
  분리 이전되었는지 같이 기록한다
- 제거 후 남은 `gateway` 전용 internal branch, helper, adapter가 정리되었는지 기록한다

## 11. 제거 대상 상세 목록

`gateway` 제거는 public API 삭제만 의미하지 않는다.
최종 목표는 public/internal/protocol/문서/테스트에서 `gateway` family 개념을
함께 제거하는 것이다.

### 11.1 public API / type / constant

아래 public symbol은 제거 대상으로 본다.

- `zlink_gateway_new(...)`
- `zlink_gateway_destroy(...)`
- `zlink_gateway_attach_discovery(...)`
- `zlink_gateway_bind(...)`
- `zlink_gateway_connect(...)`
- `zlink_gateway_disconnect(...)`
- `zlink_gateway_send(...)`
- `zlink_gateway_send_rid(...)`
- `zlink_gateway_recv(...)`
- `zlink_gateway_set_lb_strategy(...)`
- `zlink_gateway_update_peer_weight(...)`
- `zlink_gateway_status_snapshot(...)`
- `zlink_registry_gateway_peers_snapshot(...)`
- `zlink_registry_gateway_peers_query(...)`
- `zlink_registry_query_gateway_peers_snapshot(...)`
- `zlink_gateway_lb_strategy_t`
- `ZLINK_GATEWAY_LB_*`
- `zlink_gateway_monitor_event_mask_t`
- `ZLINK_GATEWAY_MONITOR_EVENT_*`
- `zlink_gateway_state_t`
- `zlink_gateway_status_t`
- `zlink_registry_gateway_peer_entry_t`
- `zlink_registry_gateway_peer_filter_t`
- `ZLINK_SERVICE_TYPE_GATEWAY`
- `ZLINK_SERVICE_ROLE_GATEWAY`

### 11.2 public header / comment / snapshot field

아래 public surface 잔여물도 함께 정리한다.

- `core/include/zlink.h` 안의 `gateway` 전용 함수 선언
- `core/include/zlink.h` 안의 `gateway` 전용 enum/struct/constant
- `core/include/zlink.h` 안의 `gateway` 관련 설명 주석과 사용 예시
- registry snapshot/count 구조체에 남아 있는 `gateway` 전용 count field

### 11.3 internal implementation file

아래 internal 구현 파일은 우선 제거 대상으로 본다.
generic helper가 정말 필요하면 `gateway` 삭제 이후 별도 surface로 다시 도입한다.

- `core/src/api/service_gateway_api.cpp`
- `core/src/services/gateway/gateway.hpp`
- `core/src/services/gateway/gateway_access.hpp`
- `core/src/services/gateway/gateway_access.cpp`
- `core/src/services/gateway/gateway_facade.cpp`
- `core/src/services/gateway/gateway_lifecycle.cpp`
- `core/src/services/gateway/gateway_monitor.cpp`
- `core/src/services/gateway/gateway_pool.cpp`
- `core/src/services/gateway/gateway_refresh.cpp`
- `core/src/services/gateway/gateway_runtime.hpp`
- `core/src/services/gateway/gateway_socket.cpp`
- `core/src/services/gateway/routing_id_utils.hpp`

### 11.4 internal type / helper / branch

아래 성격의 내부 요소도 제거 대상으로 본다.

- `gateway_t`, `gateway_access_t` 같은 gateway 전용 internal type
- gateway 전용 runtime/lifecycle/helper/monitor adapter
- poller의 gateway subject kind와 refcount 경로
- service option API 안의 gateway 전용 분기
- monitor open/snapshot API 안의 gateway 전용 분기와 provider
- discovery/registry가 가진 gateway 전용 summary/cache/store
- registry/discovery query path의 gateway 전용 filter/entry serialization

### 11.5 discovery / registry protocol

아래 discovery/registry protocol 요소도 우선 제거 대상으로 본다.
generic metadata/member 모델이 정말 필요하면
`gateway` 삭제 이후 후속 metadata 작업에서 더 작은 새 contract로 다시 도입한다.

- gateway peer report/query/reply message type
- gateway peer summary/report flush path
- gateway peer key/entry/filter/store 자료구조
- gateway peer query handler / reply serializer
- gateway peer snapshot/count 통계
- discovery protocol 안의 gateway service-type/service-role special case

### 11.6 test / perf / doc / bindings

아래 검증/문서 surface도 함께 정리한다.

- gateway 전용 unit/integration/e2e test executable과 regression
- `core/perf`의 gateway benchmark/binary/run-script/config 이름과 경로 정리
- `core/perf`에서 gateway API, gateway monitor event, gateway service token을 참조하는 코드 제거 또는 대체
- gateway 전용 테스트 helper, lane 언급, README 설명
- `doc/spec/core/gateway*.md`와 gateway 전용 plan/doc
- bindings의 `Gateway` wrapper와 예시 문서

### 11.7 제거 완료 판정

`gateway` 제거 완료는 아래를 모두 만족할 때로 본다.

- public header에서 `gateway` family symbol이 사라진다
- internal source tree에 `services/gateway/`와 이에 준하는 gateway 전용 구현이 남지 않는다
- poller/monitor/option/discovery/registry에 gateway special case branch가 남지 않는다
- gateway 전용 protocol message, struct, store, query path가 사라진다
- 테스트/`core/perf`/문서/bindings에서 gateway 개념이 제거되거나 대체 경로로 정리된다
- 삭제 후 관련 코드가 POSD 관점에서 다시 정리되어
  shallow wrapper와 임시 우회 경로가 남지 않는다

## 12. 실행 체크리스트

아래 체크리스트는 source tree 기준이다.
build 산출물이나 generated artifact는 제거 작업의 판정 기준에 포함하지 않는다.

### 12.1 public header 정리

- `core/include/zlink.h`에서 `zlink_gateway_*` 함수 선언 삭제
- `core/include/zlink.h`에서 `zlink_gateway_lb_strategy_t` 삭제
- `core/include/zlink.h`에서 `zlink_gateway_monitor_event_mask_t`와
  `ZLINK_GATEWAY_MONITOR_EVENT_*` 삭제
- `core/include/zlink.h`에서 `zlink_gateway_state_t`,
  `zlink_gateway_status_t` 삭제
- `core/include/zlink.h`에서 `zlink_registry_gateway_peer_entry_t`,
  `zlink_registry_gateway_peer_filter_t` 삭제
- `core/include/zlink.h`에서 `zlink_registry_gateway_peers_*` 선언 삭제
- `core/include/zlink.h`에서 `ZLINK_SERVICE_TYPE_GATEWAY`,
  `ZLINK_SERVICE_ROLE_GATEWAY` 삭제
- `core/include/zlink.h`에서 gateway 관련 주석/설명/example 제거
- registry snapshot/count struct에 남아 있는
  `gateway_peer_entry_count` field 제거

### 12.2 gateway service 구현 삭제

- `core/src/api/service_gateway_api.cpp` 삭제
- `core/src/services/gateway/gateway.hpp` 삭제
- `core/src/services/gateway/gateway_access.hpp` 삭제
- `core/src/services/gateway/gateway_access.cpp` 삭제
- `core/src/services/gateway/gateway_facade.cpp` 삭제
- `core/src/services/gateway/gateway_lifecycle.cpp` 삭제
- `core/src/services/gateway/gateway_monitor.cpp` 삭제
- `core/src/services/gateway/gateway_pool.cpp` 삭제
- `core/src/services/gateway/gateway_refresh.cpp` 삭제
- `core/src/services/gateway/gateway_runtime.hpp` 삭제
- `core/src/services/gateway/gateway_socket.cpp` 삭제
- `core/src/services/gateway/routing_id_utils.hpp` 삭제 또는
  gateway 비의존 공용 helper로 이동

### 12.3 generic API 분기 정리

- `core/src/api/service_option_api.cpp`의 gateway 전용 분기 제거
- `core/src/api/service_poller_api.cpp`의 gateway registration/modify/remove 분기 제거
- `core/src/api/poller_api_internal.hpp`의 `poller_subject_gateway` 제거
- `core/src/api/monitor_service_open_api.cpp`의 gateway monitor open 경로 제거
- `core/src/api/monitor_service_snapshot_api.cpp`의
  gateway snapshot provider 제거
- `core/src/api/monitor_api_internal.hpp`의 gateway 관련 전방 선언과
  provider 선언 제거
- `core/src/api/service_mode_api.cpp`의 gateway mode state/transition/poller-ref
  경로 제거

### 12.4 discovery / registry 모델 정리

- `core/src/services/discovery/discovery.hpp`의 `class gateway_t` friend 관계 제거
- `core/src/services/discovery/discovery.hpp`의
  `gateway_peer_key_t`, `gateway_peer_summary_t`,
  `_gateway_peer_summary_store` 제거
- `core/src/services/discovery/discovery.hpp`의
  `upsert_gateway_peer_summary`, `flush_gateway_peer_reports` 제거
- `core/src/services/discovery/discovery_state.cpp`의
  gateway peer summary upsert 로직 제거
- `core/src/services/discovery/discovery_access.hpp/.cpp`의
  gateway peer summary access 경로 제거
- `core/src/services/discovery/discovery_runtime_internal.hpp`의
  gateway report flush hook 제거
- `core/src/services/discovery/discovery_uplink.cpp`의
  gateway peer report send/flush/store-cleanup 로직 제거
- `core/src/services/discovery/discovery_update.cpp`의
  gateway peer report flush 호출 제거
- `core/src/services/discovery/registry.hpp`의
  `gateway_peer_key_t`, `gateway_peer_entry_t`, `_gateway_peers` 제거
- `core/src/services/discovery/registry.hpp`의
  `handle_gateway_peer_report`, `handle_gateway_peer_query`,
  `send_gateway_peer_reply`, `upsert_gateway_peer_entry` 제거
- `core/src/services/discovery/registry_state.cpp`의
  gateway peer upsert 로직 제거
- `core/src/services/discovery/registry.cpp`의
  gateway peer report/query dispatch 및 handler 제거
- `core/src/services/discovery/registry_access.hpp/.cpp`의
  gateway peer snapshot/query API 제거
- `core/src/services/discovery/registry_query_access.hpp/.cpp`의
  gateway peer query client path 제거

### 12.5 discovery protocol 정리

- `core/src/services/discovery/discovery_protocol.hpp`의
  `msg_gateway_peer_report`, `msg_gateway_peer_query`,
  `msg_gateway_peer_reply` 제거
- `core/src/services/discovery/discovery_protocol.hpp`의
  `service_type_gateway_receiver`, `service_role_gateway` 제거
- `core/src/services/discovery/discovery_protocol.hpp`의
  gateway service-type/service-role special case 제거
- gateway 전용 frame serialization/deserialization 경로 제거

### 12.6 helper 의존성 정리

- `core/src/services/discovery/socket_discovery_attachment.cpp`의
  `services/gateway/routing_id_utils.hpp` 의존 제거
- `core/src/services/discovery/discovery_bootstrap.cpp`의
  `services/gateway/routing_id_utils.hpp` 의존 제거
- `core/src/services/discovery/discovery_registry_client.cpp`의
  `services/gateway/routing_id_utils.hpp` 의존 제거
- `core/src/services/discovery/discovery_uplink.cpp`의
  `services/gateway/routing_id_utils.hpp` 의존 제거

### 12.7 core 테스트 정리

- `core/tests/e2e/discovery/test_gateway.cpp` 우선 삭제하고,
  필요한 회귀만 새 contract 기준 테스트로 이전
- `core/tests/integration/discovery/test_gateway_handover.cpp` 우선 삭제하고,
  필요한 회귀만 새 contract 기준 테스트로 이전
- `core/tests/integration/discovery/test_gateway_with_handler.cpp` 우선 삭제하고,
  필요한 회귀만 새 contract 기준 테스트로 이전
- `core/tests/integration/monitoring/test_gateway_monitor_process.cpp` 우선 삭제하고,
  필요한 회귀만 새 contract 기준 테스트로 이전
- `core/tests/README.md`의 gateway lane/test 설명 제거 또는 갱신

### 12.8 core perf 정리

- `core/perf/CMakeLists.txt`의 `perf_gateway`, `comp_src_gateway_server`,
  `comp_src_gateway_client` target 우선 제거
- `core/perf/run_comparison.py`와 `core/perf/single/run_comparison.py`의
  gateway binary/token/profile 매핑 우선 제거
- `core/perf/single/src/perf_gateway.cpp` 우선 삭제
- `core/perf/multi/src/perf_multi_gateway_server.cpp` 우선 삭제
- `core/perf/multi/src/perf_multi_gateway_client.cpp` 우선 삭제
- `core/perf/common/perf_infra.hpp`의 `perf_bind_gateway_endpoint` 우선 제거
- `core/perf/single/common/bench_common.hpp`,
  `core/perf/multi/common/perf_common.hpp`의 gateway helper 우선 제거
- `core/perf/single/tests/test_run_comparison_policy.py`의 gateway expectation 제거 또는 갱신
- `core/perf`에서 `zlink_gateway_*`, `ZLINK_GATEWAY_*`, `perf_gateway`,
  `comp_src_gateway_*` 검색 시 의도된 migration 설명 외에 잔여물이 남지 않도록 정리

### 12.9 bindings / 문서 정리

- source 기준 bindings의 `Gateway` wrapper, enum, test, example 제거
- bindings 문서의 gateway 섹션 제거 또는 대체 경로로 갱신
- `doc/spec/core/gateway.ko.md`, `doc/spec/core/gateway.md` 제거 또는
  migration 문서로 대체
- service/discovery/registry 관련 문서에서 gateway family 언급 정리

### 12.10 최종 검증

- source tree에서 `gateway` 검색 시 의도된 migration/doc 설명 외에
  삭제 대상 코드가 남지 않는지 확인
- `core/include/zlink.h`에서 gateway family symbol이 남지 않는지 확인
- discovery/registry protocol에서 gateway 전용 message/role/type이
  남지 않는지 확인
- `core/perf`에서 gateway binary/profile/token/API 참조가 남지 않는지 확인
- 삭제 후 실제로 새 contract를 도입했다면,
  metadata/member query surface와 raw profile 기반 테스트가 그 contract를 검증하는지 확인
- 삭제 후 남은 discovery/registry/service/perf 코드가
  POSD 기준으로 더 짧게 설명 가능한 구조인지 점검
- `gateway` 제거 이후에도 남은 helper/adapter/branch가
  deep module을 만들지 못하는 얕은 wrapper라면 함께 제거 또는 통합

## 13. 테스트 방향

- `gateway` metadata-aware routing contract 고정
- raw socket metadata-aware profile 결과 비교 회귀
- metadata propagation과 routing policy 반영 검증
- routing attribute query와 topology/introspection query의 역할 분리 검증

## 14. 리스크

- `gateway` 제거를 서두르면 현재 사용자-facing 편의와 관측 surface를 잃을 수 있다
- raw socket profile로 흡수하는 과정에서 오히려 사용자 개념 수가 늘 수 있다
- 삭제 후 실제 공백을 잘못 판단하면 불필요한 metadata layer를 다시 만들 위험이 있다
- public symbol만 제거하고 internal dead code를 남기면 구조 단순화 효과가 약해질 수 있다

## 15. 현재 권고

- 이번 묶음 작업에서는 `gateway` 삭제를 먼저 수행한다
- public API 삭제에 그치지 않고 주변 service/discovery/registry 코드를
  함께 단순화하는 리팩토링까지 한 작업으로 본다
- 삭제 직후에는 POSD 관점으로 관련 코드를 다시 훑어
  hidden coupling, change amplification, shallow wrapper를 추가로 제거한다
- 기본값은 `gateway` 삭제로 끝내는 것이다
- 삭제 후 비는 요구가 실제로 확인될 때만 `socket-metadata-sharing-plan.ko.md`에서
  generic metadata/member query contract를 다시 설계한다
- raw socket profile로 바로 대체 가능한 것은 migration 문서로 정리하고,
  공통 인프라가 꼭 필요한 부분만 새 surface를 추가한다

## 16. 열린 질문

- `gateway` 제거 직후 꼭 필요한 migration 문서 항목은 무엇인가
- 삭제 후에도 살아남아야 할 최소 metadata/member query contract는 무엇인가
- monitoring/introspection 중 generic topology query로 옮길 항목은 어디까지인가
