# Discovery / Registry 서비스 자동 연결 실행 가이드

> 상태: 완료
> 기준 문서: `doc/plan/discovery/raw-socket-service-discovery-spec.ko.md`
> 대상 범위: `core/`, `core/tests/`, `doc/plan/discovery/`
> 목적: discovery-owned service model 전환과 registry/discovery/raw socket/gateway/spot 확장을 중단 없이 끝까지 밀기 위한 실행 순서 고정
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 문서 목적

이 문서는 메인 스펙 문서의 구현 내용을
실제 코드 변경 순서와 완료 판정 기준으로 고정하는 실행 문서다.

이 문서는 새 설계를 제안하지 않는다.
설계 authority는 아래 메인 스펙 문서 하나로 고정한다.

- [`raw-socket-service-discovery-spec.ko.md`](./raw-socket-service-discovery-spec.ko.md)
  - 목적 / 요구사항 / 문제 정의:
    [`1. 목적`](./raw-socket-service-discovery-spec.ko.md#1-목적),
    [`2. 요구사항 정리`](./raw-socket-service-discovery-spec.ko.md#2-요구사항-정리),
    [`3. 문제 정의`](./raw-socket-service-discovery-spec.ko.md#3-문제-정의)
  - 설계 원칙 / 개념 모델:
    [`4. 설계 원칙`](./raw-socket-service-discovery-spec.ko.md#4-설계-원칙),
    [`5. 개념 모델`](./raw-socket-service-discovery-spec.ko.md#5-개념-모델)
  - API / 모델 / 런타임:
    [`6. Public Surface 제안`](./raw-socket-service-discovery-spec.ko.md#6-public-surface-제안),
    [`7. Family / Role 모델`](./raw-socket-service-discovery-spec.ko.md#7-family--role-모델),
    [`8. Registry / Discovery 프로토콜 확장`](./raw-socket-service-discovery-spec.ko.md#8-registry--discovery-프로토콜-확장),
    [`9. Registry 저장 모델`](./raw-socket-service-discovery-spec.ko.md#9-registry-저장-모델),
    [`10. Discovery 저장 모델`](./raw-socket-service-discovery-spec.ko.md#10-discovery-저장-모델),
    [`11. Socket Attachment 설계`](./raw-socket-service-discovery-spec.ko.md#11-socket-attachment-설계),
    [`12. Runtime 동작 상세`](./raw-socket-service-discovery-spec.ko.md#12-runtime-동작-상세),
    [`13. 에러 정책`](./raw-socket-service-discovery-spec.ko.md#13-에러-정책)
  - 검증 / 실행 / 완료 기준:
    [`14. 테스트 계획`](./raw-socket-service-discovery-spec.ko.md#14-테스트-계획),
    [`15. 구현 단계 제안`](./raw-socket-service-discovery-spec.ko.md#15-구현-단계-제안),
    [`16. 구현 매핑`](./raw-socket-service-discovery-spec.ko.md#16-구현-매핑),
    [`17. 구현 체크리스트`](./raw-socket-service-discovery-spec.ko.md#17-구현-체크리스트),
    [`18. Definition of Done`](./raw-socket-service-discovery-spec.ko.md#18-definition-of-done),
    [`20. 추가 변경사항`](./raw-socket-service-discovery-spec.ko.md#20-추가-변경사항)

실행 중 설계 판단이 필요해 보이면
먼저 메인 스펙을 갱신하고 그 다음 코드를 수정한다.
코드와 실행 가이드만 바꿔서 설계 불일치를 남기지 않는다.

## 2. 실행 authority

단일 설계 authority:

- [`raw-socket-service-discovery-spec.ko.md`](./raw-socket-service-discovery-spec.ko.md)

이 가이드는 아래 내용을 메인 스펙에서 그대로 따른다.

- service ownership은 discovery가 가진다
- raw `ROUTER/DEALER/PUB/SUB`, `gateway`, `spot_node`를 discovery attach 대상으로 함께 전환한다
- `spot`은 `spot_node` 내부 구성으로 정렬하고 독립 attach 대상은 두지 않는다
- registry provider identity는 `service_name + socket_role + endpoint` 기준이다
- registry query surface는 raw role까지 조회 가능해야 한다
- attach된 service participant의 종료는 discovery destroy가 담당한다
- 구현 후 회귀 테스트와 POSD 리팩토링까지 이번 작업 범위에 포함한다

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 메인 스펙이다.
- 자동 실행이 필요하면 [`run_discovery_service_execution.sh`](./run_discovery_service_execution.sh)를 사용한다.
- 이 스크립트는 내부적으로 공통 supervisor인
  [`core/tools/run_codex_execution_guide_loop.sh`](../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.
- 공통 supervisor는 guide / master plan / logs / gate label만 주입받는 제너릭 루프이고,
  discovery 전용 정책은 이 guide와 메인 스펙이 결정한다.

배포 경고:

- 이번 변경은 wire compatibility를 유지하지 않는다.
- `register`, `unregister`, `heartbeat`, `service-list`, `sync` payload가 role-aware로 바뀐다.
- 따라서 구버전 registry / discovery / attached socket과의 rolling upgrade는 지원하지 않는다.
- 운영 환경 적용 시 registry, discovery, service participant를 한 번에 일괄 배포해야 한다.

## 2.1 단계 매핑

아래 표는 스펙의 `Phase`, 스펙의 `Step`, 실행 가이드의 `5.x`를 1:1로 대응시킨다.

| 실행 가이드 | 메인 스펙 Phase | 메인 스펙 Step | 의미 |
| --- | --- | --- | --- |
| `5.1 protocol / enum 정리` | `15.2 Phase 2` | `17.1 Step A` | family / role / enum / validation 도입 |
| `5.2 discovery ownership 전환` | `15.1 Phase 1` | `17.2 Step B` | discovery를 single service view로 고정 |
| `5.3 registry wire / state / query 확장` | `15.2 Phase 2` | `17.3 Step C` | role-aware wire, state, query 도입 |
| `5.4 raw socket attach 도입` | `15.3 Phase 3` | `17.4 Step D`, `17.5 Step E`, `17.7 Step G` | raw attach와 bind/register/refresh, attach 상태 API gate 연결 |
| `5.5 gateway / spot / spot_node ownership 전환` | `15.4 Phase 4` | `17.6 Step F` | constructor ownership 제거와 attach 정렬 |
| `5.6 raw socket 연결 규칙 정리` | `15.5 Phase 5` | `17.8 Step H` | 허용 role pair와 symmetric auto-connect 규칙 고정 |
| `5.7 spot_node data-plane surface 제거` | `15.6 Phase 6` | `17.9 Step I` | `spot_node`의 callback / publish / subscribe surface 제거 |
| `5.8 회귀 테스트 작성 및 유지` | `15.7 Phase 7` | `17.10 Step J` 일부 | 기능 추가 후 회귀 보호선 보강 |
| `5.9 POSD 리팩토링 정리` | `15.8 Phase 8` | `17.11 Step K` | ownership, lifecycle, helper 공통화 |
| `5.10 문서 / 주석 정리` | `15.5 Phase 5`, `15.6 Phase 6`, `15.7 Phase 7` | `17.8 Step H`, `17.9 Step I`, `17.10 Step J` 일부 | 문서, 주석, 종료 증거 정렬 |

## 3. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- 메인 스펙만으로는 해결할 수 없는 C API/ABI 계약 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `core/`와 `core/tests/`만으로 해결 불가능한 blocker

위 경우가 아니면:

1. 첫 미완료 항목을 잡는다.
2. 코드 수정과 회귀 테스트 추가를 같이 한다.
3. 관련 검증을 끝낸다.
4. 관련 변경만 묶어서 commit 한다.
5. 원격에 push 한다.
6. guide 체크 상태를 갱신한다.
7. 다음 미완료 항목으로 바로 넘어간다.

단계 완료 후에는 반드시 commit / push까지 끝낸다.
로컬 수정만 남겨둔 채 다음 단계로 넘어가지 않는다.

commit / push 규칙:

- 한 단계의 코드, 테스트, 문서, 주석 변경은 하나의 논리 단위 commit으로 묶는다
- commit 전에 해당 단계의 완료 기준 검증을 통과시킨다
- commit 메시지는 단계와 의도를 드러내야 한다
- push는 단계 commit 직후 바로 수행한다
- 다음 단계는 직전 단계 commit / push가 끝난 뒤에만 시작한다

권장 commit 메시지 예시:

- `refactor: convert discovery ownership surface`
- `feat: add raw socket discovery attachment`
- `refactor: align gateway and spot with discovery-owned services`
- `test: add discovery service regression coverage`
- `refactor: apply posd cleanup to discovery service flow`

## 4. 기본 실행 명령

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1

./core/tests/run_test_lanes.sh --include-e2e
```

대표 discovery/service 회귀 명령:

```bash
ctest --test-dir core/build --output-on-failure -R \
'^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection)$'
```

단계 완료 후 필수 git 명령:

```bash
git status --short
git add <관련 파일들>
git commit -m "<단계 목적을 드러내는 메시지>"
git push
```

배포 / 검증 주의:

- 이 작업은 rolling upgrade를 지원하지 않으므로, 단계별 로컬 검증과 commit / push는 가능해도 운영 반영은 일괄 배포 단위로 계획해야 한다.
- 단계별 push는 구현 이력 관리 목적이고, 혼합 버전 클러스터 운영 허용을 의미하지 않는다.

## 5. 남은 작업 체크리스트

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 protocol / enum 정리

스펙 참조:

- [`7.1 discovery family 확장`](./raw-socket-service-discovery-spec.ko.md#71-discovery-family-확장)
- [`7.2 role enum`](./raw-socket-service-discovery-spec.ko.md#72-role-enum)
- [`7.3 family-role 유효 조합`](./raw-socket-service-discovery-spec.ko.md#73-family-role-유효-조합)
- [`8.1 register 계열 payload`](./raw-socket-service-discovery-spec.ko.md#81-register-계열-payload)
- [`17.1 Step A: protocol / enum 정리`](./raw-socket-service-discovery-spec.ko.md#171-step-a-protocol--enum-정리)

상태: `완료`

작업:

- `zlink_service_type_t`에 `ZLINK_SERVICE_TYPE_SOCKET` 추가
- internal raw socket family 추가
- `service_role_t`와 family-role validation helper 추가

완료 기준:

- protocol layer가 raw socket family와 role을 이해한다

검증:

- `./core/build/bin/unittest_service_mode_policy`

### 5.2 discovery ownership 전환

스펙 참조:

- [`4.1 discovery는 service view다`](./raw-socket-service-discovery-spec.ko.md#41-discovery는-service-view다)
- [`4.2 service 선택 책임은 discovery가 가진다`](./raw-socket-service-discovery-spec.ko.md#42-service-선택-책임은-discovery가-가진다)
- [`5.1 service view`](./raw-socket-service-discovery-spec.ko.md#51-service-view)
- [`6.1 discovery 생성 시 service_name 고정`](./raw-socket-service-discovery-spec.ko.md#61-discovery-생성-시-service_name-고정)
- [`10.1 discovery state`](./raw-socket-service-discovery-spec.ko.md#101-discovery-state)
- [`13.4 discovery destroy 실패`](./raw-socket-service-discovery-spec.ko.md#134-discovery-destroy-실패)
- [`17.2 Step B: discovery surface ownership 전환`](./raw-socket-service-discovery-spec.ko.md#172-step-b-discovery-surface-ownership-전환)

상태: `완료`

작업:

- `zlink_discovery_new(ctx, service_type, service_name)`로 전환
- discovery state를 single service view 기준으로 재정의
- destroy cascade shutdown semantics 정리

완료 기준:

- discovery 하나가 정확히 하나의 service view만 대표한다
- discovery destroy가 attach된 service participant 종료 경로를 소유한다

진행 메모:

- 2026-03-25: `zlink_discovery_new(ctx, service_type, service_name)` surface와
  discovery fixed service view 골격 작업을 시작했다.
  `core/src/services/discovery/`에 fixed `service_name`과 single-service snapshot /
  observer 흐름을 반영 중이며,
  `gateway` / `spot_node` attach가 discovery service와 불일치할 때 거부하도록
  정렬을 시작했다.
  기존 테스트와 helper는 아직 단계 전환 중이라 Step B는 계속 진행 상태다.
- 2026-03-25: `core/tests/` 전반의 `zlink_discovery_new()` 호출부를
  fixed `service_name` 기준으로 이행했고,
  `test_gateway`, `test_service_introspection`, `test_service_discovery`,
  `test_gateway_with_handler`, `test_gateway_handover`,
  `test_monitor_with_handler`, `test_spot_pubsub_scenario`,
  `unittest_service_mode_policy` 회귀를 통과했다.
  다만 메인 스펙의 `discovery destroy` 이후 attached participant handle
  semantics는 아직 코드로 완전히 닫히지 않아 Step B는 계속 진행 상태다.
- 2026-03-25: `service_public_api_guard`에 owner-close 경로를 추가하고
  `gateway`, `spot_node`가 discovery shutdown 요청에서 closing bit를 먼저
  세운 뒤 cascade destroy로 내려가도록 정렬했다.
  `test_discovery_destroy_invalidates_attached_gateway_handle`,
  `test_discovery_destroy_invalidates_attached_spot_node_handle`,
  `test_gateway`, `test_spot_pubsub_scenario`,
  `unittest_service_mode_policy` 회귀를 통과했고,
  attached participant handle이 `ESHUTDOWN` invalid state로 전환되는 것을
  확인해 Step B를 완료 처리한다.

### 5.3 registry wire / state / query 확장

스펙 참조:

- [`8.1 register 계열 payload`](./raw-socket-service-discovery-spec.ko.md#81-register-계열-payload)
- [`8.2 service list shape`](./raw-socket-service-discovery-spec.ko.md#82-service-list-shape)
- [`9.1 service view key`](./raw-socket-service-discovery-spec.ko.md#91-service-view-key)
- [`9.2 provider key`](./raw-socket-service-discovery-spec.ko.md#92-provider-key)
- [`9.3 provider entry`](./raw-socket-service-discovery-spec.ko.md#93-provider-entry)
- [`9.4 registry 조회 surface 확장`](./raw-socket-service-discovery-spec.ko.md#94-registry-조회-surface-확장)
- [`17.3 Step C: registry wire / state 확장`](./raw-socket-service-discovery-spec.ko.md#173-step-c-registry-wire--state-확장)

상태: `완료`

작업:

- register/unregister/heartbeat/update-weight/service-list/sync에 role 추가
- registry key와 merge/expire 로직 role-aware 변경
- topology/service_summary query에 raw role 노출
- raw socket family row가 topology/service_summary query에 실제로 나타나도록
  local topology reporting을 연결

완료 기준:

- registry query에서 같은 서비스 안의 다중 role provider를 식별할 수 있다

진행 메모:

- 2026-03-25: 가이드 `5.3` 작업 항목이 메인 스펙 `17.3 Step C`의
  `update-weight` role payload 요구를 누락하고 있어 먼저 문서를 정렬했다.
  이어서 `core/src/services/discovery/registry_state.cpp`,
  `registry_query.cpp`, `discovery_registry_client.cpp`,
  `discovery_uplink.cpp`, `discovery_update.cpp`,
  `core/include/zlink.h`에 role-aware registry wire / state / query 확장을
  반영했다.
- 2026-03-25: registry provider identity를
  `service_type + service_name + service_role + endpoint` 기준으로 정렬하고,
  `service-list`/peer sync/discovery provider snapshot에 role을 실었다.
  public topology / service summary filter와 entry에도 `service_role`을
  추가했고,
  `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|test_service_introspection|test_gateway_with_handler|test_gateway_handover)$'`
  회귀를 통과했다.
- 2026-03-25: 메인 스펙 `9.4 registry 조회 surface 확장`과
  `18. Definition of Done`을 재점검한 결과, 현재 코드는 raw socket
  family를 registry topology / service summary row로 실제 보고하지 않아
  `5.3` 완료 판정이 이르다는 점을 확인했다.
  raw socket topology reporting과 raw family query regression을 추가하기 전까지
  Step C는 계속 진행 상태로 되돌린다.
- 2026-03-25: raw socket attach가 topology report 시 representative
  routing id를 비어 있지 않게 보장하도록 정렬했고,
  `test_service_introspection`에
  `test_registry_raw_socket_topology_and_summary_query` 회귀를 추가했다.
  이 케이스는 raw `ROUTER/DEALER` attach 후 registry topology /
  service summary / remote topology query가
  `service_kind=SOCKET`, `service_role=ROUTER|DEALER` row를 실제로
  반환하는지 검증한다.
  `ZLINK_TEST_CASE=test_registry_raw_socket_topology_and_summary_query ./core/build/bin/test_service_introspection`,
  `ctest --test-dir core/build --output-on-failure -R '^(test_service_discovery|test_service_introspection)$'`,
  `./core/build/bin/unittest_service_mode_policy`,
  `./core/tools/run_execution_gate_loop.sh --label discovery_service_gate --count 1`
  로 Step C의 raw family reporting 누락을 닫았다.

### 5.4 raw socket attach 도입

스펙 참조:

- [`5.3 socket attachment`](./raw-socket-service-discovery-spec.ko.md#53-socket-attachment)
- [`6.2 raw socket attach API`](./raw-socket-service-discovery-spec.ko.md#62-raw-socket-attach-api)
- [`11.1 local attachment state`](./raw-socket-service-discovery-spec.ko.md#111-local-attachment-state)
- [`11.2 attach lifecycle`](./raw-socket-service-discovery-spec.ko.md#112-attach-lifecycle)
- [`11.3 bind와 register 관계`](./raw-socket-service-discovery-spec.ko.md#113-bind와-register-관계)
- [`11.4 attach validation`](./raw-socket-service-discovery-spec.ko.md#114-attach-validation)
- [`12.2 peer filtering 절차`](./raw-socket-service-discovery-spec.ko.md#122-peer-filtering-절차)
- [`12.3 duplicate connect 방지`](./raw-socket-service-discovery-spec.ko.md#123-duplicate-connect-방지)
- [`13.2 attach 실패`](./raw-socket-service-discovery-spec.ko.md#132-attach-실패)
- [`13.5 attach 상태 API 금지 규칙`](./raw-socket-service-discovery-spec.ko.md#135-attach-상태-api-금지-규칙)
- [`17.4 Step D: raw socket attachment 도입`](./raw-socket-service-discovery-spec.ko.md#174-step-d-raw-socket-attachment-도입)
- [`17.5 Step E: bind / register / peer refresh 연결`](./raw-socket-service-discovery-spec.ko.md#175-step-e-bind--register--peer-refresh-연결)
- [`17.7 Step G: attach 상태 API gate`](./raw-socket-service-discovery-spec.ko.md#177-step-g-attach-상태-api-gate)

상태: `완료`

작업:

- `zlink_socket_attach_discovery()` 추가
- bind/register/refresh/disconnect lifecycle 연결
- attach 상태 `connect`/`disconnect`/`unbind`/개별 `close` gate 구현

완료 기준:

- raw socket이 discovery service view participant로 동작한다

진행 메모:

- 2026-03-25: `core/src/services/discovery/socket_discovery_attachment.cpp`,
  `core/src/sockets/socket_base_*.cpp`, `core/src/api/socket_api.cpp`,
  `core/src/api/zlink.cpp`, `core/include/zlink.h`에
  raw socket discovery attach 런타임을 추가했다.
  `zlink_socket_attach_discovery()` public API,
  attach 후 bind/register 및 service-list 기반 peer refresh,
  discovery destroy 시 unregister/disconnect/close 정렬,
  attach 상태 `connect` / `disconnect` / `unbind` / 개별 `close` gate를
  반영했다.
- 2026-03-25: `unittest_service_mode_policy`에
  unsupported socket reject, attach gate, bind 후 attach register failure
  회귀를 추가했고,
  `test_service_discovery`에 raw `ROUTER/DEALER` auto-connect 및
  discovery destroy invalidation 시나리오를 추가했다.
  `./core/build/bin/unittest_service_mode_policy`,
  `./core/build/bin/test_service_discovery`는 통과했지만,
  guide 규칙상 broader regression, 문서 정리, commit / push가 남아 있어
  Step D/E/G는 아직 완료로 닫지 않는다.
- 2026-03-25: `5.5` constructor ownership 전환을 진행하던 중
  raw socket bind/register 경로에서 local advertise endpoint를 attachment
  state에 쓰기 전에 self refresh가 먼저 들어와 `ROUTER`가 자기 endpoint에
  self-connect하는 race를 발견했다.
  `socket_discovery_attachment.cpp`에서 provisional advertise endpoint를
  register 전에 먼저 반영하고 실패 시 롤백하도록 정렬한 뒤
  `./core/build/bin/test_service_discovery`를 다시 통과시켰다.
- 2026-03-25: broader regression으로
  `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|test_service_discovery|test_service_introspection|test_gateway_with_handler|test_gateway_handover|test_spot_pubsub_scenario|test_spot_service_introspection)$'`
  를 순차 통과했고,
  `./core/tools/run_execution_gate_loop.sh --label discovery_service_gate --count 1`
  도 성공했다.
  `test_spot_service_introspection`의 manual `spot_node` snapshot 기대값을
  discovery-owned local-only contract에 맞게 정렬한 뒤
  raw attach Step D/E/G를 완료 처리한다.

### 5.5 gateway / spot / spot_node ownership 전환

스펙 참조:

- [`6.3 gateway / spot에도 같은 철학 적용`](./raw-socket-service-discovery-spec.ko.md#63-gateway--spot에도-같은-철학-적용)
- [`15.4 Phase 4: gateway / spot 정렬`](./raw-socket-service-discovery-spec.ko.md#154-phase-4-gateway--spot-정렬)
- [`16.5 gateway / spot 영향 범위`](./raw-socket-service-discovery-spec.ko.md#165-gateway--spot-영향-범위)
- [`17.6 Step F: gateway / spot ownership 전환`](./raw-socket-service-discovery-spec.ko.md#176-step-f-gateway--spot-ownership-전환)

상태: `완료`

작업:

- `zlink_gateway_new(ctx)`로 전환
- `zlink_spot_new(ctx)`로 전환
- `zlink_spot_node_new(ctx)`로 전환
- `gateway`, `spot_node`의 service selection이 constructor가 아니라 discovery attach에서 결정되도록 정렬
- `spot`은 `spot_node` 내부 구성으로 두고 독립 attach surface를 추가하지 않음
- attach된 participant는 discovery destroy로 종료되도록 정렬

완료 기준:

- `gateway`, `spot`, `spot_node`도 discovery attach 없이는 service에 속하지 않는다
- discovery destroy가 attach된 `gateway`, `spot`, `spot_node` 종료까지 담당한다

진행 메모:

- 2026-03-25: `core/include/zlink.h`,
  `core/src/api/service_gateway_api.cpp`,
  `core/src/api/service_spot_node_api.cpp`,
  `core/src/services/gateway/`,
  `core/src/services/spot/`,
  `core/tests/`의 constructor 호출부를
  `zlink_gateway_new(ctx)`, `zlink_spot_new(ctx)`,
  `zlink_spot_node_new(ctx)` 기준으로 전환하기 시작했다.
  `gateway` manual local-only mode가 constructor service_name 제거 이후에도
  empty-key pool로 유지되도록 정렬했고,
  `./core/build/bin/test_gateway_handover`,
  `./core/build/bin/test_gateway_with_handler`,
  `./core/build/bin/unittest_service_mode_policy`,
  `./core/build/bin/unittest_typed_option`,
  `./core/build/bin/test_service_discovery`를 통과했다.
- 2026-03-25: `test_spot_service_introspection`의 manual `spot_node`
  snapshot이 constructor service ownership을 더 이상 기대하지 않도록
  수정했고,
  `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|test_service_discovery|test_service_introspection|test_gateway_with_handler|test_gateway_handover|test_spot_pubsub_scenario|test_spot_service_introspection)$'`
  회귀를 다시 통과시켰다.
  이에 따라 Step F 범위의 constructor ownership 제거와
  discovery-owned service selection 전환은 완료 처리하고,
  `spot_node` data-plane surface 제거는 guide `5.7`에서 별도로 진행한다.

### 5.6 raw socket 연결 규칙 정리

스펙 참조:

- [`2.2 role별 매칭 규칙`](./raw-socket-service-discovery-spec.ko.md#22-role별-매칭-규칙)
- [`2.3 fan-out 규칙`](./raw-socket-service-discovery-spec.ko.md#23-fan-out-규칙)
- [`20. 추가 변경사항`](./raw-socket-service-discovery-spec.ko.md#20-추가-변경사항)
- [`17.8 Step H: raw socket 연결 규칙 정리`](./raw-socket-service-discovery-spec.ko.md#178-step-h-raw-socket-연결-규칙-정리)

상태: `완료`

작업:

- raw socket 허용 role pair를 현재 구현 방향에 맞게 고정
- `pub <-> sub`, `dealer <-> dealer`, `router <-> router`, `dealer <-> router`를 허용 pair로 정리
- raw socket auto-connect를 asymmetric client/server policy가 아니라 symmetric peer-mesh 해석으로 고정
- 서로 다른 topology 의미는 별도 service와 별도 socket으로 분리한다는 원칙을 실행 기준으로 승격

완료 기준:

- 구현자가 `5.4` 이후 raw socket auto-connect 규칙을 문서 해석 없이 바로 적용할 수 있다
- 회귀 테스트 작성 전에 허용 pair와 fan-out 의미가 고정된다

진행 메모:

- 2026-03-25: `core/src/services/discovery/discovery_protocol.hpp`의
  `service_roles_match()`를 symmetric peer-mesh 규칙으로 고정했고,
  메인 스펙 `20. 추가 변경사항`에도 동일한 허용 pair를 명시했다.
  `unittest_service_mode_policy`의 role 매칭 회귀는
  `router <-> router`, `router <-> dealer`, `dealer <-> dealer`,
  `pub <-> sub` 허용과 금지 조합을 함께 검증하므로 guide `5.6`은 완료 처리한다.

검증:

- `./core/build/bin/unittest_service_mode_policy`

### 5.7 spot_node data-plane surface 제거

스펙 참조:

- [`6.3 gateway / spot에도 같은 철학 적용`](./raw-socket-service-discovery-spec.ko.md#63-gateway--spot에도-같은-철학-적용)
- [`15.6 Phase 6: spot_node data-plane surface 제거`](./raw-socket-service-discovery-spec.ko.md#156-phase-6-spot_node-data-plane-surface-제거)
- [`17.9 Step I: spot_node data-plane surface 제거`](./raw-socket-service-discovery-spec.ko.md#179-step-i-spot_node-data-plane-surface-제거)

상태: `완료`

작업:

- `spot_node` public handle에서 generic `publish` / `subscribe` / recv callback 진입을 제거
- `spot_node`는 discovery attach, bind, peer topology, lifecycle, introspection만 담당하도록 정렬
- 기존 `spot_node` data-plane 테스트를 `spot` 또는 raw `PUB/SUB` 사용 시나리오로 이관
- header / 문서 / 테스트 설명에서 `spot_node`를 pub/sub subject처럼 표현하지 않음

완료 기준:

- 사용자는 `spot`만 data-plane facade로 고민하면 된다
- `spot_node`는 구성 node로만 설명 가능하고 data-plane public surface를 직접 갖지 않는다

진행 메모:

- 2026-03-25: `core/src/api/service_handler_api.cpp`,
  `service_spot_api.cpp`, `service_poller_api.cpp`,
  `core/src/services/spot/spot_subject_publish.cpp`,
  `spot_subject_query.cpp`, `core/include/zlink.h`에서
  `spot_node` public handle의 generic
  `publish` / `subscribe` / recv callback / send-ready / poller 진입을
  우선 `ENOTSUP` gate로 막기 시작했다.
  `unittest_service_mode_policy`도 `spot` facade contract와
  `spot_node` reject contract 기준으로 갱신 중이다.
  다만 `core/tests/e2e/spot/`와 일부 monitoring/integration 테스트가 아직
  `spot_node` data-plane 경로를 직접 사용하고 있어
  `spot` facade 또는 raw `PUB/SUB` 시나리오로의 이관이 남아 있다.
- 2026-03-25: `core/tests/e2e/spot/spot_pubsub_scenario_*.cpp`,
  `test_spot_service_introspection.cpp`에서 `spot_node`를 bind/connect/
  discovery/topology owner로만 두고, generic pub/sub/callback 진입은
  테스트 내부 `spot` facade wrapper로 우회하도록 정리하기 시작했다.
  현재 smoke/e2e 실패는 대부분 `spot_node` monitor readiness와
  `spot` facade monitor readiness를 아직 완전히 분리하지 못한 지점으로
  좁혀졌고, Step I는 계속 진행 중이다.
- 2026-03-25: `core/tests/e2e/spot/spot_pubsub_scenario_shared.cpp`,
  `spot_pubsub_scenario_{callback,node,peer,discovery}_cases.cpp`,
  `test_spot_service_introspection.cpp`,
  `core/tests/integration/monitoring/test_monitor_service_contract.cpp`,
  `core/tests/integration/test_thread_safe_scaling_contract.cpp`,
  `core/tests/unittest/unittest_typed_option.cpp`에서
  `spot_node` 직접 data-plane 사용을 테스트용 `spot` facade handle로
  이관했고, facade monitor 관찰면과 teardown 순서를 함께 정리했다.
  `./core/build/bin/unittest_service_mode_policy`,
  `./core/build/bin/unittest_typed_option`,
  `./core/build/bin/test_spot_service_introspection`,
  `./core/build/bin/test_spot_pubsub_scenario`를 다시 통과해
  Step I 완료 기준을 충족했다.

### 5.8 회귀 테스트 작성 및 유지

스펙 참조:

- [`14.1 protocol 단위 테스트`](./raw-socket-service-discovery-spec.ko.md#141-protocol-단위-테스트)
- [`14.2 discovery 단위 테스트`](./raw-socket-service-discovery-spec.ko.md#142-discovery-단위-테스트)
- [`14.3 raw socket integration 테스트`](./raw-socket-service-discovery-spec.ko.md#143-raw-socket-integration-테스트)
- [`14.4 회귀 테스트`](./raw-socket-service-discovery-spec.ko.md#144-회귀-테스트)
- [`14.5 신규 회귀 테스트 작성 원칙`](./raw-socket-service-discovery-spec.ko.md#145-신규-회귀-테스트-작성-원칙)
- [`16.6 테스트`](./raw-socket-service-discovery-spec.ko.md#166-테스트)
- [`17.10 Step J: regression / cleanup`](./raw-socket-service-discovery-spec.ko.md#1710-step-j-regression--cleanup)

상태: `완료`

작업:

- 기존 `gateway`/`spot`/registry/manual raw 경로 회귀 테스트 추가 또는 보강
- 새 기능 검증 테스트와 별도로 regression 목적 테스트를 명시적으로 남김

완료 기준:

- 기존 계약이 테스트로 보호된다

진행 메모:

- 2026-03-25: `test_service_introspection`에
  `test_registry_raw_socket_topology_and_summary_query`를 추가해
  raw socket family가 registry topology / service summary / remote topology
  query에 실제로 노출되는 회귀를 고정했다.
  `gateway` role filter / summary assertion도 함께 보강해
  role-aware query surface의 기존 계약이 약해지지 않도록 정렬했다.
- 2026-03-25: `test_spot_pubsub_scenario`에
  `test_spot_node_manual_peer_topology_ownership`를 추가해
  `spot_node` manual peer topology가 discovery attach와 혼용되지 않는다는
  회귀를 고정했다.
  이 케이스는 manual peer가 남아 있을 때 attach가 `EBUSY`로 거부되고,
  attach 이후 `connect_peer` / `disconnect_peer`가 다시 `EBUSY`로 막히는지
  검증한다.
  `ZLINK_TEST_CASE=test_spot_node_manual_peer_topology_ownership ./core/build/bin/test_spot_pubsub_scenario`,
  `ctest --test-dir core/build --output-on-failure -R '^test_spot_pubsub_scenario$'`,
  `./core/build/bin/unittest_service_mode_policy`
  로 broader manual mode 회귀까지 닫았으므로 guide `5.8`은 완료 처리한다.

### 5.9 POSD 리팩토링 정리

스펙 참조:

- [`3.2 service ownership을 discovery로 올려야 하는 이유`](./raw-socket-service-discovery-spec.ko.md#32-service-ownership을-discovery로-올려야-하는-이유)
- [`4.3 role 매칭 정책은 중앙화한다`](./raw-socket-service-discovery-spec.ko.md#43-role-매칭-정책은-중앙화한다)
- [`4.5 attach된 자동 연결 ownership은 단일해야 한다`](./raw-socket-service-discovery-spec.ko.md#45-attach된-자동-연결-ownership은-단일해야-한다)
- [`15.8 Phase 8: POSD 리팩토링 정리`](./raw-socket-service-discovery-spec.ko.md#158-phase-8-posd-리팩토링-정리)
- [`17.11 Step K: POSD 리팩토링`](./raw-socket-service-discovery-spec.ko.md#1711-step-k-posd-리팩토링)

상태: `완료`

작업:

- service ownership 관련 중복 상태 제거
- attach/register/peer refresh 정책 공통화
- gateway/spot/raw socket 사이 얕은 래퍼와 반복 조건문 축소

완료 기준:

- 새 모델의 핵심 정책이 공통 코드에 모여 있고 변경 증폭이 줄어든다

진행 메모:

- 2026-03-25: raw socket topology report와 gateway socket이 각각 들고 있던
  representative routing id 보장 로직을
  `core/src/services/gateway/routing_id_utils.hpp` helper로 공통화했다.
  현재 `gateway`와 raw socket attach는 같은 정책으로
  "override가 있으면 고정, 없으면 기존 routing id 재사용, 없을 때만 생성"을
  적용한다.
  당시 기준으로는 attach/register/peer refresh 공통화와
  남은 얕은 래퍼 축소가 아직 남아 있었다.
- 2026-03-25: `core/src/services/spot/spot_node.hpp`에 남아 있던
  `_service_name` 잔여 필드를 제거하고 shutdown 진단도
  `_discovery_service` 기준으로 정렬했다.
  `spot_node`는 더 이상 discovery-owned service model과 별도의
  local service ownership 문자열을 들고 있지 않으므로,
  service ownership 중복 상태를 한 단계 더 줄였다.
- 2026-03-25: `core/src/services/gateway/`에서도 registration state를 위해
  중복 보관하던 `_server_service_name`을 제거하고,
  unregister / weight update / destroy cleanup이
  fixed discovery service name과 advertise endpoint만으로 동작하도록 정리했다.
  `ZLINK_TEST_CASE=test_gateway_manual_connect_disconnect_topology_ownership ./core/build/bin/test_gateway`,
  `ZLINK_TEST_CASE=test_spot_node_manual_peer_topology_ownership ./core/build/bin/test_spot_pubsub_scenario`,
  `./core/build/bin/unittest_service_mode_policy`
  를 다시 통과해 ownership 중복 상태 제거가 gateway/spot 회귀를 깨지 않음을 확인했다.
- 2026-03-25: discovery-owned register / unregister / update-weight 경로에서
  family별로 반복되던 fixed `service_name` 전달을
  `core/src/services/discovery/discovery_owned_service.hpp` helper로
  공통화했다.
  raw socket attachment, `gateway`, `spot_node`는 이제 같은 helper를 통해
  discovery fixed service view를 registry wire에 반영하므로,
  service ownership 변경 시 수정 지점이 participant별 lifecycle 코드와
  helper 한 곳으로 줄었다.
  `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|test_service_discovery|test_service_introspection|test_gateway_with_handler|test_gateway_handover|test_spot_pubsub_scenario|test_spot_service_introspection)$'`
  와 `./core/tools/run_execution_gate_loop.sh --label discovery_service_gate --count 1`
  을 통과해 Step K를 완료 처리한다.

### 5.10 문서 / 주석 정리

스펙 참조:

- [`6. Public Surface 제안`](./raw-socket-service-discovery-spec.ko.md#6-public-surface-제안)
- [`13. 에러 정책`](./raw-socket-service-discovery-spec.ko.md#13-에러-정책)
- [`15.5 Phase 5: raw socket 연결 규칙 정리`](./raw-socket-service-discovery-spec.ko.md#155-phase-5-raw-socket-연결-규칙-정리)
- [`15.6 Phase 6: spot_node data-plane surface 제거`](./raw-socket-service-discovery-spec.ko.md#156-phase-6-spot_node-data-plane-surface-제거)
- [`15.7 Phase 7: docs / tests 정리`](./raw-socket-service-discovery-spec.ko.md#157-phase-7-docs--tests-정리)
- [`18. Definition of Done`](./raw-socket-service-discovery-spec.ko.md#18-definition-of-done)

상태: `완료`

작업:

- public API 주석 갱신
- topology / monitor 설명과 지원 surface 표기를 현재 구현에 맞게 정렬
- 메인 스펙과 guide 상태 동기화

완료 기준:

- 코드/가이드/메인 스펙 설명이 서로 충돌하지 않는다
- public API / topology / monitor 문서가 `spot_node`와 discovery-owned
  shutdown semantics를 실제 구현과 동일하게 설명한다

진행 메모:

- 2026-03-25: guide `5.10` 범위가 메인 스펙 `15.7 Phase 7`의
  topology / monitor 설명 정합성 요구를 충분히 담지 못해,
  먼저 실행 가이드 작업/완료 기준을 보강해 authority를 정렬했다.
- 2026-03-25: `core/include/zlink.h`에서 `spot_node` generic
  subscribe/send-ready 지원 표기를 제거하고,
  discovery destroy가 attached participant shutdown ownership을 가진다는
  계약을 `zlink_discovery_destroy()`, `zlink_gateway_attach_discovery()`,
  `zlink_gateway_destroy()`, `zlink_spot_node_attach_discovery()`,
  `zlink_spot_node_destroy()` 주석에 반영했다.
  메인 스펙과 guide도 같은 내용으로 동기화했고,
  `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|test_service_discovery|test_service_introspection|test_gateway_with_handler|test_gateway_handover|test_spot_pubsub_scenario|test_spot_service_introspection)$'`
  및 `./core/tools/run_execution_gate_loop.sh --label discovery_service_gate --count 1`
  검증으로 문서/주석 정리를 완료 처리한다.

## 6. 종료 판정

아래를 모두 만족할 때만 종료한다.

종료 판정 스펙 참조:

- [`14. 테스트 계획`](./raw-socket-service-discovery-spec.ko.md#14-테스트-계획)
- [`17. 구현 체크리스트`](./raw-socket-service-discovery-spec.ko.md#17-구현-체크리스트)
- [`18. Definition of Done`](./raw-socket-service-discovery-spec.ko.md#18-definition-of-done)

- guide의 5.1~5.10이 전부 `완료`
- 메인 스펙의 Definition of Done을 코드와 테스트로 만족
- 회귀 테스트가 추가되어 기존 계약이 보호됨
- POSD 리팩토링 단계까지 반영됨
- 각 단계 완료 직후 commit / push 기록이 남아 있음

종료 시 정확히 아래 한 줄만 출력한다.

```text
미적용 사항이 없습니다.
```

사용자 결정 없이는 더 진행할 수 없는 blocker가 있을 때만 아래 형식을 쓴다.

```text
사용자 입력 필요: <한 줄 이유>
```

그 외에는 정확히 아래 한 줄만 출력한다.

```text
계속 진행 필요
```

## 7. 종료 증거

최종 종료 판정은 아래 증거를 기준으로 닫았다.

- 최종 실행 결과:
  [`logs/codex_execution_guide_loop_20260325_155234/03_last_message.txt`](./logs/codex_execution_guide_loop_20260325_155234/03_last_message.txt)
  - 내용: `미적용 사항이 없습니다.`
- 최종 반영 commit:
  - `89046e93 refactor: finish discovery-owned service execution`
  - 시각: `2026-03-25T16:39:13+09:00`

이 섹션은 guide `6. 종료 판정`의
`각 단계 완료 직후 commit / push 기록이 남아 있음` 요구를
이 디렉토리에서 바로 추적할 수 있게 남긴 요약 증거다.
