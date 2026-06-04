# Discovery 자동 연결 타입 구현 실행 계획

> 상태: 구현 및 최종 검증 완료
> 기준 문서: `doc/spec/draft/auto-connect-channel-types.ko.md`
> 대상 범위: `core/`, `bindings/`, `samples/`, `doc/`, `doc/site/`
> 목적: Discovery 자동 연결 타입 설계를 사용자 개입 없이 구현, 검증, 리팩토링, 문서 반영, 바인딩 배포 준비까지 끝내는 실행 순서를 고정한다.
> 최종 종료 판정: `미반영 항목이 없습니다. POSD 리팩토링 후보가 없습니다. 전체 테스트와 sample/perf smoke가 모두 통과했습니다.`

## 1. 문서 목적

이 문서는 [`auto-connect-channel-types.ko.md`](../../spec/draft/auto-connect-channel-types.ko.md)의
구현을 끝까지 진행하기 위한 실행 계획이다.

이 문서는 새 설계를 제안하지 않는다. 설계 authority는 아래 draft spec 하나로
고정한다.

- [`doc/spec/draft/auto-connect-channel-types.ko.md`](../../spec/draft/auto-connect-channel-types.ko.md)

구현 중 draft spec과 충돌하는 판단이 필요하면 먼저 draft spec을 수정하고
그 다음 구현과 이 계획을 맞춘다. 코드만 바꾸고 spec을 남겨 두면 완료로 보지
않는다.

## 2. 실행 원칙

- 사용자에게 추가 판단을 요구하지 않고 진행한다.
- 호환성 유지는 목표가 아니다.
- 명령, 경로, runner 옵션이 실제 저장소와 다르면 먼저 `--help`와 주변 README를
  확인해 같은 의미의 실행 방법으로 고치고, 이 계획 문서도 함께 갱신한다.
- 테스트나 smoke가 실패하면 실패를 우회하지 않는다. 원인을 수정하고 같은 gate를
  다시 실행한다.
- 이미 존재하는 사용자 변경은 되돌리지 않는다. 직접 관련된 파일에서 충돌이 나면
  기존 변경을 읽고 그 위에 맞춰 구현한다.
- 사용자 개입이 필요한 blocker는 작업을 진행할 수 없는 환경 문제로 한정한다.
  예를 들어 필수 toolchain이 설치되어 있지 않고 저장소 안의 대체 runner도 없을 때만
  blocker로 기록한다.
- 구현은 draft spec의 공개 API, enum, 구조체, errno, lifecycle, 테스트 요구사항을
  모두 반영해야 한다.
- 각 단계는 코드, 테스트, 문서, 바인딩 영향까지 함께 닫는다.
- 구현을 마친 뒤에는 spec 반영 리뷰를 반복한다. 미반영 항목이 0개가 되기 전에는
  다음 단계로 넘어가지 않는다.
- 전체 테스트가 통과한 뒤에만 POSD 기반 리팩토링 단계로 들어간다.
- POSD 리팩토링도 반복한다. 의미 있는 리팩토링 후보가 0개가 되기 전에는 다음
  단계로 넘어가지 않는다.
- 모든 sample과 perf smoke가 통과한 뒤에만 정식 문서와 바인딩 native runtime
  갱신 단계로 넘어간다.
- 정식 문서를 반영한 뒤에도 sample/perf smoke를 다시 실행한다.

## 3. 완료 판정 보드

상태 값은 아래 값만 사용한다.

- `pending`: 시작 전
- `in_progress`: 구현 또는 검토 중
- `review`: 완료 주장 후 직접 리뷰 중
- `rework`: 리뷰에서 누락이 발견되어 수정 중
- `blocked`: 사용자 작업 충돌이나 환경 문제로 진행 불가
- `done`: 완료 기준 통과

| 단계 | 상태 | 완료 조건 |
|------|------|-----------|
| 1. 설계 항목 분해 | done | draft spec의 모든 요구사항이 추적 항목으로 나뉜다 |
| 2. core public API 구현 | done | `core/include/zlink.h`와 C API 구현이 새 계약을 제공한다 |
| 3. Discovery/Registry runtime 구현 | done | channel 계약, role 검증, 자동 연결 방향, lifecycle이 동작한다 |
| 4. core 테스트 구현 | done | draft spec의 테스트 요구사항 20개 이상이 자동 테스트로 검증된다 |
| 5. spec 반영 리뷰 반복 | done | draft spec 대비 미반영 항목이 0개다 |
| 6. 전체 core 테스트 | done | core 전체 테스트가 통과한다 |
| 7. POSD 리팩토링 반복 | done | 의미 있는 POSD 리팩토링 후보가 0개다 |
| 8. bindings API 구현 | done | 모든 언어 바인딩이 새 API와 enum을 노출한다 |
| 9. sample/perf 1차 smoke | done | C와 언어별 sample/perf smoke가 실패 없이 통과한다 |
| 10. 정식 문서 반영 | done | `doc/internals`, `doc/spec`, `doc/guide`, `doc/spec/bindings`가 최신 계약과 일치한다 |
| 11. native runtime 갱신 | done | 각 바인딩 native runtime이 새 core/c binding 산출물로 갱신된다 |
| 12. sample/perf 최종 smoke | done | native 갱신 뒤 모든 sample/perf smoke가 다시 통과한다 |
| 13. 최종 종료 리뷰 | done | 코드, 테스트, 문서, 바인딩, perf 증거가 모두 남아 있다 |

## 4. 설계 항목 추적표

아래 표는 구현자가 draft spec을 빠뜨리지 않기 위한 추적표다.
각 항목은 draft 근거 링크를 따라 원문 계약을 확인하고, 구현 후 `증거` 칸에
파일, 테스트, 명령 결과를 적어야 한다.

| ID | 요구사항 | draft 근거 | 상태 | 증거 |
|----|----------|------------|------|------|
| AC-01 | `zlink_auto_connect_type_t` 공개 enum 추가 | [추가되는 enum](../../spec/draft/auto-connect-channel-types.ko.md#추가되는-enum) | done | §15 최종 증거 |
| AC-02 | `zlink_discovery_new(ctx, auto_connect_type, channel_name)`로 변경 | [변경되는 Discovery 생성 API](../../spec/draft/auto-connect-channel-types.ko.md#변경되는-discovery-생성-api), [Discovery 생성 계약](../../spec/draft/auto-connect-channel-types.ko.md#discovery-생성-계약) | done | §15 최종 증거 |
| AC-03 | Discovery/Registry API에서 `zlink_service_type_t` 인자 제거 | [변경되는 Discovery 생성 API](../../spec/draft/auto-connect-channel-types.ko.md#변경되는-discovery-생성-api), [변경되는 Registry 조회 API](../../spec/draft/auto-connect-channel-types.ko.md#변경되는-registry-조회-api) | done | §15 최종 증거 |
| AC-04 | `zlink_discovery_set_dealer_peer_mode()`와 관련 enum 제거 | [제거되는 API와 enum](../../spec/draft/auto-connect-channel-types.ko.md#제거되는-api와-enum), [기존 DEALER peer mode 제거](../../spec/draft/auto-connect-channel-types.ko.md#기존-dealer-peer-mode-제거) | done | §15 최종 증거 |
| AC-05 | Registry member 조회 API를 `channel_name` 기준으로 변경 | [변경되는 Registry 조회 API](../../spec/draft/auto-connect-channel-types.ko.md#변경되는-registry-조회-api) | done | §15 최종 증거 |
| AC-06 | `zlink_registry_member_peer_metadata()`의 role 인자를 `zlink_service_role_t`로 변경 | [변경되는 Registry 조회 API](../../spec/draft/auto-connect-channel-types.ko.md#변경되는-registry-조회-api) | done | §15 최종 증거 |
| AC-07 | member/service/topology 구조체에 `auto_connect_type`, `channel_name` 반영 | [service/member/topology 구조체 변경](../../spec/draft/auto-connect-channel-types.ko.md#servicemembertopology-구조체-변경), [Discovery snapshot 계약](../../spec/draft/auto-connect-channel-types.ko.md#discovery-snapshot-계약), [Topology summary 계약](../../spec/draft/auto-connect-channel-types.ko.md#topology-summary-계약) | done | §15 최종 증거 |
| AC-08 | summary/topology filter의 zero-value 의미 구현 | [service/member/topology 구조체 변경](../../spec/draft/auto-connect-channel-types.ko.md#servicemembertopology-구조체-변경) | done | §15 최종 증거 |
| AC-09 | Registry channel 계약 저장소 추가 | [Registry 채널 계약](../../spec/draft/auto-connect-channel-types.ko.md#registry-채널-계약) | done | §15 최종 증거 |
| AC-10 | 같은 channel의 다른 자동 연결 타입을 `EEXIST`로 거부 | [같은 채널에서 타입이 다른 경우](../../spec/draft/auto-connect-channel-types.ko.md#같은-채널에서-타입이-다른-경우), [에러](../../spec/draft/auto-connect-channel-types.ko.md#에러) | done | §15 최종 증거 |
| AC-11 | Registry channel 계약을 Registry process lifetime 동안 유지 | [Registry 채널 계약](../../spec/draft/auto-connect-channel-types.ko.md#registry-채널-계약) | done | §15 최종 증거 |
| AC-12 | Registry cluster conflict에서 deterministic winner 계약을 채택하고 loser provider를 projection에서 제거 | [Registry 채널 계약](../../spec/draft/auto-connect-channel-types.ko.md#registry-채널-계약) | done | §15 최종 증거 |
| AC-13 | provider registration마다 channel 계약과 role 재검증 | [Registry 등록 시점](../../spec/draft/auto-connect-channel-types.ko.md#registry-등록-시점) | done | §15 최종 증거 |
| AC-14 | provider registration 실패 시 attach/bind 상태 원자 rollback | [Registry 등록 시점](../../spec/draft/auto-connect-channel-types.ko.md#registry-등록-시점) | done | §15 최종 증거 |
| AC-15 | endpoint 없는 consumer는 member snapshot에서 제외하되 자동 connect 허용 | [Endpoint와 참여 상태](../../spec/draft/auto-connect-channel-types.ko.md#endpoint와-참여-상태), [Discovery snapshot 계약](../../spec/draft/auto-connect-channel-types.ko.md#discovery-snapshot-계약) | done | §15 최종 증거 |
| AC-16 | `ROUTE_MESH`는 ROUTER만 허용하고 pairwise single initiator 적용 | [Pairwise initiator 규칙](../../spec/draft/auto-connect-channel-types.ko.md#pairwise-initiator-규칙), [ROUTE_MESH](../../spec/draft/auto-connect-channel-types.ko.md#route_mesh) | done | §15 최종 증거 |
| AC-17 | `CLIENT_SERVER`는 DEALER가 모든 eligible ROUTER endpoint에 connect | [CLIENT_SERVER](../../spec/draft/auto-connect-channel-types.ko.md#client_server) | done | §15 최종 증거 |
| AC-18 | `DEALER_MESH`는 DEALER만 허용하고 pairwise single initiator 적용 | [Pairwise initiator 규칙](../../spec/draft/auto-connect-channel-types.ko.md#pairwise-initiator-규칙), [DEALER_MESH](../../spec/draft/auto-connect-channel-types.ko.md#dealer_mesh) | done | §15 최종 증거 |
| AC-19 | `FANOUT`은 SUB가 PUB endpoint에 connect | [FANOUT](../../spec/draft/auto-connect-channel-types.ko.md#fanout) | done | §15 최종 증거 |
| AC-20 | `SPOT_MESH`는 SpotNode만 허용하고 pairwise single initiator 적용 | [Pairwise initiator 규칙](../../spec/draft/auto-connect-channel-types.ko.md#pairwise-initiator-규칙), [SPOT_MESH](../../spec/draft/auto-connect-channel-types.ko.md#spot_mesh) | done | §15 최종 증거 |
| AC-21 | attach API는 Discovery handle의 자동 연결 타입으로 role 검증 | [유지되는 attach API](../../spec/draft/auto-connect-channel-types.ko.md#유지되는-attach-api), [Attach 검증 순서](../../spec/draft/auto-connect-channel-types.ko.md#attach-검증-순서) | done | §15 최종 증거 |
| AC-22 | `zlink_spot_node_attach_channel_dealer()`는 `CLIENT_SERVER`와 `DEALER_MESH`만 허용 | [유지되는 attach API](../../spec/draft/auto-connect-channel-types.ko.md#유지되는-attach-api), [Attach 검증 순서](../../spec/draft/auto-connect-channel-types.ko.md#attach-검증-순서) | done | §15 최종 증거 |
| AC-23 | `zlink_discovery_resolve_spot()`은 `SPOT_MESH` 외에는 `ENOTSUP` | [Discovery 조회 API 검증](../../spec/draft/auto-connect-channel-types.ko.md#discovery-조회-api-검증) | done | §15 최종 증거 |
| AC-24 | Discovery-managed raw socket과 SpotNode의 manual lifecycle 제한 유지 | [Public lifecycle](../../spec/draft/auto-connect-channel-types.ko.md#public-lifecycle) | done | §15 최종 증거 |
| AC-25 | Discovery destroy가 discovery-managed endpoint를 정리 | [Public lifecycle](../../spec/draft/auto-connect-channel-types.ko.md#public-lifecycle) | done | §15 최종 증거 |
| AC-26 | `service_kind`는 진단 필드로만 남기고 자동 연결 판단에 쓰지 않음 | [Topology summary 계약](../../spec/draft/auto-connect-channel-types.ko.md#topology-summary-계약) | done | §15 최종 증거 |
| AC-27 | public option은 추가하지 않고 dealer peer mode option 제거 | [public option 변경](../../spec/draft/auto-connect-channel-types.ko.md#public-option-변경), [기존 DEALER peer mode 제거](../../spec/draft/auto-connect-channel-types.ko.md#기존-dealer-peer-mode-제거) | done | §15 최종 증거 |
| AC-28 | 바인딩별 enum, 생성자, 조회 API, 제거 API를 새 계약에 맞춤 | [Public API 변경 요약](../../spec/draft/auto-connect-channel-types.ko.md#public-api-변경-요약), [구현 순서](../../spec/draft/auto-connect-channel-types.ko.md#구현-순서) | done | §15 최종 증거 |
| AC-29 | sample이 새 자동 연결 타입 API를 사용 | [예시](../../spec/draft/auto-connect-channel-types.ko.md#예시) | done | §15 최종 증거 |
| AC-30 | perf runner가 새 자동 연결 타입 API로 빌드되고 실행 | [구현 순서](../../spec/draft/auto-connect-channel-types.ko.md#구현-순서) | done | §15 최종 증거 |

## 5. 구현 순서

### 5.1 설계 항목 분해

작업:

- draft spec을 읽고 `4. 설계 항목 추적표`의 누락 항목을 보완한다.
- `core/include/zlink.h`의 현재 public API와 draft spec을 diff 형태로 비교한다.
- `core/src/services/discovery`, `core/src/services/spot`,
  `core/src/api`, `core/tests`의 영향 범위를 기록한다.
- 각 바인딩의 FFI 선언, domain enum, sample, perf runner 영향 범위를 기록한다.

완료 기준:

- draft spec의 의미 있는 문장이 추적표나 테스트 요구사항 중 하나로 연결된다.
- 추적표에 `기타` 같은 포괄 항목이 남지 않는다.

### 5.2 core public API 구현

작업:

- `core/include/zlink.h`에 `zlink_auto_connect_type_t`를 추가한다.
- `zlink_discovery_new()` 시그니처를 자동 연결 타입과 channel name 기준으로 바꾼다.
- Registry member 조회 API 시그니처를 channel name 기준으로 바꾼다.
- member, service summary, topology summary, topology filter 구조체를 draft spec과 맞춘다.
- dealer peer mode API와 enum을 제거한다.
- public errno mapping 문서와 코드가 `EINVAL`, `ENOTSUP`, `EBUSY`, `EEXIST`를
  draft spec과 같이 반환하도록 맞춘다.

완료 기준:

- `core/include/zlink.h`만 봐도 새 Discovery channel 계약을 이해할 수 있다.
- 제거 API를 참조하는 core 코드와 테스트가 남아 있지 않다.

검증:

```bash
rg -n "zlink_discovery_set_dealer_peer_mode|zlink_discovery_dealer_peer_mode_t" \
  core bindings samples \
  -g '!**/build/**' -g '!**/node_modules/**' -g '!**/.gradle/**'
rg -n "zlink_discovery_new\\(" core bindings samples doc
cmake --build core/build -j"$(nproc)"
```

### 5.3 Discovery/Registry runtime 구현

작업:

- Discovery state를 `auto_connect_type + channel_name` 기준으로 정리한다.
- Registry bootstrap payload에 `auto_connect_type`, `channel_name`,
  `discovery_routing_id`를 포함한다.
- Registry에 channel contract 저장소를 추가하고 process lifetime 동안 유지한다.
- provider registration, unregister, heartbeat, metadata update, service-list,
  peer sync payload를 channel contract 기준으로 정렬한다.
- Registry cluster conflict winner 규칙을 구현한다.
- role derivation helper를 만들고 raw socket/SpotNode attach 검증에서 공통으로 쓴다.
- peer refresh는 자동 연결 타입별로 대상과 방향을 계산한다.
- mesh 타입은 pairwise single initiator helper를 공유한다.
- `service_kind` 기반 자동 연결 판단을 제거한다.
- provider registration 실패 rollback을 attach 후 bind, bind 후 attach 양쪽에서 검증한다.

완료 기준:

- 자동 연결 타입별 허용 role과 연결 방향이 한 곳의 정책 helper로 설명된다.
- Registry channel contract와 Discovery local state가 서로 다른 의미를 갖지 않는다.
- 실패한 참여자가 snapshot, topology, service summary에 남지 않는다.

### 5.4 core 테스트 구현

draft spec의 테스트 요구사항을 최소한 아래 자동 테스트로 닫는다.

| 번호 | 테스트 내용 | draft 근거 | 상태 |
|------|-------------|------------|------|
| T-01 | 같은 channel name과 같은 자동 연결 타입 Discovery 여러 개 성공 | [규칙](../../spec/draft/auto-connect-channel-types.ko.md#규칙) | done |
| T-02 | 같은 channel name과 다른 자동 연결 타입 중 하나만 성공 | [같은 채널에서 타입이 다른 경우](../../spec/draft/auto-connect-channel-types.ko.md#같은-채널에서-타입이-다른-경우), [동시 등록](../../spec/draft/auto-connect-channel-types.ko.md#동시-등록) | done |
| T-03 | 타입 충돌 실패 참여자가 member snapshot에 없음 | [규칙](../../spec/draft/auto-connect-channel-types.ko.md#규칙), [Topology summary 계약](../../spec/draft/auto-connect-channel-types.ko.md#topology-summary-계약) | done |
| T-04 | 마지막 provider unregister 뒤에도 channel 계약 유지 | [Registry 채널 계약](../../spec/draft/auto-connect-channel-types.ko.md#registry-채널-계약) | done |
| T-05 | `ROUTE_MESH`에서 ROUTER 외 attach 실패 | [ROUTE_MESH](../../spec/draft/auto-connect-channel-types.ko.md#route_mesh) | done |
| T-06 | `ROUTE_MESH` ROUTER pair initiator 1개 | [Pairwise initiator 규칙](../../spec/draft/auto-connect-channel-types.ko.md#pairwise-initiator-규칙), [ROUTE_MESH](../../spec/draft/auto-connect-channel-types.ko.md#route_mesh) | done |
| T-07 | `CLIENT_SERVER` DEALER는 ROUTER에 connect, ROUTER는 DEALER에 connect 안 함 | [CLIENT_SERVER](../../spec/draft/auto-connect-channel-types.ko.md#client_server) | done |
| T-08 | `CLIENT_SERVER` DEALER는 모든 eligible ROUTER에 connect | [CLIENT_SERVER](../../spec/draft/auto-connect-channel-types.ko.md#client_server) | done |
| T-09 | `DEALER_MESH` DEALER pair initiator 1개 | [Pairwise initiator 규칙](../../spec/draft/auto-connect-channel-types.ko.md#pairwise-initiator-규칙), [DEALER_MESH](../../spec/draft/auto-connect-channel-types.ko.md#dealer_mesh) | done |
| T-10 | `FANOUT` SUB는 PUB에 connect, PUB는 SUB에 connect 안 함 | [FANOUT](../../spec/draft/auto-connect-channel-types.ko.md#fanout) | done |
| T-11 | `SPOT_MESH` SpotNode pair initiator 1개 | [Pairwise initiator 규칙](../../spec/draft/auto-connect-channel-types.ko.md#pairwise-initiator-규칙), [SPOT_MESH](../../spec/draft/auto-connect-channel-types.ko.md#spot_mesh) | done |
| T-12 | endpoint 없는 `CLIENT_SERVER` DEALER는 snapshot 제외, connect 허용 | [Endpoint와 참여 상태](../../spec/draft/auto-connect-channel-types.ko.md#endpoint와-참여-상태), [CLIENT_SERVER](../../spec/draft/auto-connect-channel-types.ko.md#client_server) | done |
| T-13 | endpoint 없는 `FANOUT` SUB는 snapshot 제외, connect 허용 | [Endpoint와 참여 상태](../../spec/draft/auto-connect-channel-types.ko.md#endpoint와-참여-상태), [FANOUT](../../spec/draft/auto-connect-channel-types.ko.md#fanout) | done |
| T-14 | Registry member peer 조회 API가 `channel_name`만 사용 | [변경되는 Registry 조회 API](../../spec/draft/auto-connect-channel-types.ko.md#변경되는-registry-조회-api) | done |
| T-15 | service summary와 topology query가 `auto_connect_type` 필터 적용 | [service/member/topology 구조체 변경](../../spec/draft/auto-connect-channel-types.ko.md#servicemembertopology-구조체-변경) | done |
| T-16 | dealer peer mode API와 enum 제거 | [제거되는 API와 enum](../../spec/draft/auto-connect-channel-types.ko.md#제거되는-api와-enum) | done |
| T-17 | SpotNode channel dealer attach 허용 타입 검증 | [유지되는 attach API](../../spec/draft/auto-connect-channel-types.ko.md#유지되는-attach-api), [Attach 검증 순서](../../spec/draft/auto-connect-channel-types.ko.md#attach-검증-순서) | done |
| T-18 | `resolve_spot`은 `SPOT_MESH`에서만 동작 | [Discovery 조회 API 검증](../../spec/draft/auto-connect-channel-types.ko.md#discovery-조회-api-검증) | done |
| T-19 | Discovery destroy가 discovery-managed endpoint 정리 | [Public lifecycle](../../spec/draft/auto-connect-channel-types.ko.md#public-lifecycle) | done |
| T-20 | Registry peer sync conflict에서 deterministic winner만 projection에 남음 | [Registry 채널 계약](../../spec/draft/auto-connect-channel-types.ko.md#registry-채널-계약) | done |

완료 기준:

- 위 20개 테스트가 자동화되어 있고, 각 테스트 이름으로 요구사항을 추적할 수 있다.
- 실패 path는 errno까지 검증한다.
- positive path는 실제 connect나 observable topology 변화까지 검증한다.

검증:

```bash
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -R 'discovery|registry|service|spot' -j1
```

## 6. spec 반영 리뷰 반복 gate

이 단계는 구현자가 완료라고 판단한 뒤 반드시 반복한다.

반복 절차:

1. draft spec을 문단 단위로 읽는다.
2. 각 문단을 `4. 설계 항목 추적표`의 항목에 연결한다.
3. 연결된 항목의 코드 증거와 테스트 증거를 확인한다.
4. 증거가 없거나 의미가 다르면 `rework`로 되돌린다.
5. 누락 항목을 수정하고 관련 테스트를 추가한다.
6. 다시 1번부터 반복한다.

다음 문장을 리뷰 로그에 쓸 수 있을 때만 통과한다.

```text
draft spec 대비 미반영 항목이 없습니다.
```

리뷰 명령 예시:

```bash
rg -n "AUTO_CONNECT|auto_connect|channel_name|service_name|dealer_peer|service_type" \
  core/include core/src core/tests bindings doc/spec/draft

rg -n "ZLINK_AUTO_CONNECT|zlink_auto_connect_type_t|zlink_discovery_new|zlink_registry_member_peers" \
  core bindings samples doc
```

## 7. 전체 core 테스트 gate

spec 반영 리뷰가 끝난 뒤 전체 core 테스트를 실행한다.

```bash
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -j"$(nproc)"
./core/tests/run_test_lanes.sh --include-e2e
```

실패가 있으면 실패 원인을 수정하고 아래 순서로 되돌아간다.

1. 관련 코드 수정
2. 관련 테스트 추가 또는 보정
3. `6. spec 반영 리뷰 반복 gate`
4. 전체 core 테스트 재실행

전체 core 테스트가 통과하기 전에는 POSD 리팩토링으로 넘어가지 않는다.

## 8. POSD 리팩토링 반복 gate

전체 core 테스트가 성공하면 POSD 기반 리팩토링을 시작한다.
판단 기준은 [`doc/principal/software-design-principles.md`](../../principal/software-design-principles.md)다.

리뷰 대상:

- Discovery channel contract 저장 위치
- Registry channel contract와 provider state의 정보 중복
- role validation과 auto-connect policy helper의 분리 수준
- pairwise initiator 계산의 중복 여부
- attach/bind/register rollback 경로의 시간적 분해 여부
- public API가 구현 세부를 호출자에게 밀어내는지 여부
- 바인딩에서 core 계약을 얕게 재노출하거나 임의 해석하는지 여부

반복 절차:

1. red flag 목록을 작성한다.
2. 각 red flag마다 위반한 POSD 원칙을 적는다.
3. 두 가지 이상 수정 방향을 비교한다.
4. 더 깊은 모듈과 더 단순한 public surface를 만드는 방향을 선택한다.
5. 리팩토링한다.
6. 관련 테스트와 전체 core 테스트를 다시 실행한다.
7. 다시 red flag 리뷰를 한다.

다음 문장을 리뷰 로그에 쓸 수 있을 때만 통과한다.

```text
의미 있는 POSD 리팩토링 후보가 없습니다.
```

검증:

```bash
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -j"$(nproc)"
```

POSD 리팩토링으로 public API나 동작이 바뀌면 `6. spec 반영 리뷰 반복 gate`로
되돌아간다.

## 9. bindings 구현 gate

대상 언어:

- `bindings/c`
- `bindings/cpp`
- `bindings/dotnet`
- `bindings/go`
- `bindings/java`
- `bindings/node`
- `bindings/python`
- `bindings/rust`

각 바인딩은 아래 항목을 모두 반영한다.

- 새 `AutoConnectType` 또는 언어별 enum 추가
- Discovery 생성자를 `auto_connect_type + channel_name` 기준으로 변경
- Registry member 조회 API를 channel name 기준으로 변경
- member/service/topology 구조체 또는 DTO에 `auto_connect_type`, `channel_name` 반영
- dealer peer mode API 제거
- `resolve_spot`의 `SPOT_MESH` 전용 실패 계약 반영
- sample과 perf 코드의 compile error 제거
- 언어별 spec 문서와 README의 public API 예제 갱신

바인딩별 기본 검증:

```bash
./bindings/c/samples/run_samples.sh
./bindings/c/perf/run_benchmarks.sh --pattern PAIR --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/c/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/cpp/tests/run_tests.sh
./bindings/cpp/samples/run_samples.sh
./bindings/cpp/perf/run_benchmarks.sh --pattern PAIR --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/cpp/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/dotnet/tests/run_tests.sh
./bindings/dotnet/samples/run_samples.sh
./bindings/dotnet/perf/run_benchmarks.sh --pattern PAIR --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/dotnet/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/go/tests/run_tests.sh
./bindings/go/samples/run_samples.sh
./bindings/go/perf/run_benchmarks.sh --pattern PAIR --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/go/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/java/tests/run_tests.sh
./bindings/java/samples/run_samples.sh
./bindings/java/perf/run_benchmarks.sh --pattern PAIR --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/java/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/node/tests/run_tests.sh
./bindings/node/samples/run_samples.sh
./bindings/node/perf/run_benchmarks.sh --pattern PAIR --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/node/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/python/tests/run_tests.sh
python3 bindings/python/examples/run_all_examples.py
./bindings/python/perf/run_benchmarks.sh --pythonpath bindings/python/src --pattern PAIR --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/python/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/rust/tests/run_tests.sh
./bindings/rust/samples/run_samples.sh
./bindings/rust/perf/run_benchmarks.sh --pattern PAIR --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/rust/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp
```

명령이 실제 runner 옵션과 다르면 runner의 `--help`를 확인해 같은 의미의 smoke
명령으로 바꾼다. 바꾼 명령은 이 문서에 다시 반영한다.

## 10. sample/perf smoke 범위

1차 smoke와 최종 smoke는 같은 범위를 실행한다.

single perf pattern:

- `PAIR`
- `PUBSUB`
- `DEALER_DEALER`
- `DEALER_ROUTER`
- `ROUTER_ROUTER`
- `SPOT`

multi perf pattern:

- `DEALER_DEALER`
- `DEALER_ROUTER`
- `ROUTER_ROUTER`
- `PUBSUB`
- `SPOT`
- `SPOT_REQREP`
- `SPOT_SENDSEND`
- `STREAM`

smoke 설정:

- runs: `1`
- duration: `1`
- transports: `tcp`
- msg sizes: `64`

패턴별 smoke는 아래 명령으로 실행한다.

```bash
SINGLE_PATTERNS="PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT"
MULTI_PATTERNS="DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,SPOT,SPOT_REQREP,SPOT_SENDSEND,STREAM"

./bindings/c/perf/run_benchmarks.sh --pattern "$SINGLE_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/c/perf/run_benchmarks_multi.sh --pattern "$MULTI_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/cpp/perf/run_benchmarks.sh --pattern "$SINGLE_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/cpp/perf/run_benchmarks_multi.sh --pattern "$MULTI_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/dotnet/perf/run_benchmarks.sh --pattern "$SINGLE_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/dotnet/perf/run_benchmarks_multi.sh --pattern "$MULTI_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/go/perf/run_benchmarks.sh --pattern "$SINGLE_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/go/perf/run_benchmarks_multi.sh --pattern "$MULTI_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/java/perf/run_benchmarks.sh --pattern "$SINGLE_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/java/perf/run_benchmarks_multi.sh --pattern "$MULTI_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/node/perf/run_benchmarks.sh --pattern "$SINGLE_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/node/perf/run_benchmarks_multi.sh --pattern "$MULTI_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/python/perf/run_benchmarks.sh --pythonpath bindings/python/src --pattern "$SINGLE_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/python/perf/run_benchmarks_multi.sh --pattern "$MULTI_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp

./bindings/rust/perf/run_benchmarks.sh --pattern "$SINGLE_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
./bindings/rust/perf/run_benchmarks_multi.sh --pattern "$MULTI_PATTERNS" --runs 1 --duration 1 --msg-sizes 64 --transports tcp
```

C perf의 `run_benchmarks_multi.sh`는 `core/build` runtime을 기준으로 한다.
`core/src`나 `core/include`를 바꾼 뒤에는 반드시 먼저 실행한다.

```bash
cmake --build core/build -j"$(nproc)"
./bindings/c/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER --runs 1 --duration 1 --msg-sizes 64 --transports tcp
```

## 11. 정식 문서 반영 gate

구현과 1차 sample/perf smoke가 끝나면 draft 내용을 정식 문서로 나누어 반영한다.

반영 대상:

- `doc/spec/core/service/discovery.ko.md`
- `doc/spec/core/service/discovery.md`
- `doc/spec/core/service/registry.ko.md`
- `doc/spec/core/service/registry.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/core/socket/router.ko.md`
- `doc/spec/core/socket/router.md`
- `doc/spec/core/socket/dealer.ko.md`
- `doc/spec/core/socket/dealer.md`
- `doc/spec/core/socket/pub.ko.md`
- `doc/spec/core/socket/pub.md`
- `doc/spec/core/socket/sub.ko.md`
- `doc/spec/core/socket/sub.md`
- `doc/spec/core/errors.ko.md`
- `doc/spec/core/errors.md`
- `doc/spec/core/errno-map.ko.md`
- `doc/spec/core/errno-map.md`
- `doc/guide/07-1-discovery.ko.md`
- `doc/guide/07-1-discovery.md`
- `doc/guide/07-3-spot.ko.md`
- `doc/guide/07-3-spot.md`
- `doc/guide/07-4-registry.ko.md`
- `doc/guide/07-4-registry.md`
- `doc/internals/discovery-internals.ko.md`
- `doc/internals/discovery-internals.md`
- `doc/internals/registry-internals.ko.md`
- `doc/internals/registry-internals.md`
- `doc/internals/spot-internals.ko.md`
- `doc/internals/spot-internals.md`
- `doc/site/docs/api/discovery.ko.md`
- `doc/site/docs/api/discovery.md`
- `doc/site/docs/api/registry.ko.md`
- `doc/site/docs/api/registry.md`
- `doc/site/docs/api/spot.ko.md`
- `doc/site/docs/api/spot.md`
- `doc/site/docs/guide/07-1-discovery.ko.md`
- `doc/site/docs/guide/07-1-discovery.md`
- `doc/site/docs/guide/07-3-spot.ko.md`
- `doc/site/docs/guide/07-3-spot.md`
- `doc/site/docs/guide/07-4-registry.ko.md`
- `doc/site/docs/guide/07-4-registry.md`
- `doc/site/docs/internals/services-internals.ko.md`
- `doc/site/docs/internals/services-internals.md`
- `doc/site/docs/internals/spot-internals.ko.md`
- `doc/site/docs/internals/spot-internals.md`
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`

작성 기준:

- `doc/spec/`에는 공개 API 계약만 쓴다.
- `doc/guide/`에는 사용 의도와 예제만 쓴다.
- `doc/internals/`에는 Registry/Discovery/Spot 내부 구조와 흐름을 쓴다.
- draft spec은 구현 후에도 변경 이력 확인용으로 남길 수 있다. 다만 정식 계약은
  `doc/spec/`와 `core/include/zlink.h`가 기준이 되게 한다.

문서 검증:

```bash
rg -n "zlink_discovery_set_dealer_peer_mode|zlink_discovery_dealer_peer_mode_t" \
  core bindings samples \
  -g '!**/build/**' -g '!**/node_modules/**' -g '!**/.gradle/**'
rg -n "ZLINK_AUTO_CONNECT|AutoConnect|auto_connect_type|channel_name" doc/spec doc/guide doc/internals doc/site/docs
rg -n "service_name" doc/spec/core/service doc/spec/bindings doc/guide/07-1-discovery* doc/site/docs/api/discovery*
rg -n "zlink_discovery_set_dealer_peer_mode|zlink_discovery_dealer_peer_mode_t" doc/spec doc/guide doc/site/docs
```

첫 번째 명령은 코드와 sample에서 제거 API가 남지 않는지 확인하며 0건이어야 한다.
두 번째 명령은 새 용어가 정식 문서에 들어갔는지 확인한다. 세 번째 명령의
`service_name` 결과는 모두 검토해 Discovery channel 계약을 설명해야 하는 위치에
남은 오래된 용어가 아닌지 분류한다. 다른 서비스 계약에서 독립적으로 쓰는
`service_name`이면 허용할 수 있지만, Discovery 자동 연결 계약을 뜻하는 위치라면
`channel_name`으로 고친다. 네 번째 명령의 제거 API 문서 결과는 정식 계약에
남아 있으면 수정한다. migration note나 구현 전 draft/plan이 아닌 정식 문서에서
새 public API처럼 보이면 완료로 보지 않는다.

AGENTS.md의 금지 표현 규칙도 함께 직접 확인한다.

## 12. native runtime 갱신 gate

정식 문서를 반영한 뒤 core와 C binding 산출물을 최신으로 다시 빌드한다.

```bash
cmake --build core/build -j"$(nproc)"
cmake --build bindings/c/build -j"$(nproc)"
```

그 다음 현재 플랫폼의 runtime을 각 바인딩 native 디렉터리에 복사한다.
Linux x86_64 기준 기본 대상은 아래와 같다. 기존 package가 versioned 파일명만
가지고 있으면 같은 basename에 새 산출물을 덮어쓴다. 기존에 없던 파일명을
임의로 추가하기 전에 loader와 packaging 규칙을 먼저 확인한다.

```bash
CORE_LIB="$(realpath core/build/lib/libzlink.so)"
C_LIB="$(realpath bindings/c/build/libzlink_c.so)"

for dir in \
  bindings/cpp/native/linux-x86_64 \
  bindings/dotnet/native/linux-x86_64 \
  bindings/dotnet/runtimes/linux-x64/native \
  bindings/go/native/linux-x86_64 \
  bindings/java/native/linux-x86_64 \
  bindings/node/native/linux-x86_64 \
  bindings/python/src/zlink/native/linux-x86_64 \
  bindings/rust/native/linux-x86_64
do
  [ -d "$dir" ] || continue
  find "$dir" -maxdepth 1 -type f -name 'libzlink.so*' \
    -exec cp "$CORE_LIB" {} \;
  find "$dir" -maxdepth 1 -type f -name 'libzlink_c.so*' \
    -exec cp "$C_LIB" {} \;
done
```

플랫폼별 디렉터리가 다르면 현재 OS/arch에 맞는 `native/<platform-arch>`를 사용한다.
`.so.5`, `.so.<version>`, `.dylib`, `.dll`, `runtimes/*/native`가 package 계약에
필요한 바인딩은 기존 파일 구성을 확인하고 같은 set을 갱신한다.

갱신 검증:

```bash
find bindings -path '*/native/*' -type f | sort
find bindings -path '*/native/*' -type f -name 'libzlink*.so*' \
  -exec sh -c 'echo "$1"; ldd "$1" >/dev/null' _ {} \;
```

## 13. 최종 sample/perf smoke gate

native runtime을 갱신한 뒤 `9. bindings 구현 gate`와 `10. sample/perf smoke 범위`의
모든 sample/perf smoke를 다시 실행한다.

실패가 있으면 아래 순서로 되돌아간다.

1. 실패한 바인딩 구현 또는 native runtime packaging 수정
2. 해당 바인딩 테스트 실행
3. sample/perf smoke 재실행
4. 문서나 native runtime packaging에 영향이 있었으면
   `11. 정식 문서 반영 gate`와 `12. native runtime 갱신 gate`를 다시 실행

## 14. 최종 종료 리뷰

최종 종료 전에 아래 명령으로 남은 흔적을 확인한다.

```bash
git status --short

# 0건이어야 하는 항목
rg -n "zlink_discovery_set_dealer_peer_mode|zlink_discovery_dealer_peer_mode_t" \
  core bindings samples \
  -g '!**/build/**' -g '!**/node_modules/**' -g '!**/.gradle/**'
rg -n "ZLINK_DISCOVERY_DEALER_PEER_MODE|dealer peer mode" core bindings samples \
  -g '!**/build/**' -g '!**/node_modules/**' -g '!**/.gradle/**'

# 감사 후 모두 분류해야 하는 항목
rg -n "zlink_discovery_new\\(" core bindings samples doc
rg -n "zlink_discovery_set_dealer_peer_mode|zlink_discovery_dealer_peer_mode_t" doc/spec doc/guide doc/site/docs
rg -n "service_name" core/include doc/spec/core/service doc/spec/bindings doc/guide/07-1-discovery* doc/site/docs/api/discovery*
rg -n "ZLINK_AUTO_CONNECT|AutoConnect|auto_connect_type|channel_name" core bindings samples doc
```

`zlink_discovery_new()` 호출은 새 API도 같은 3개 인자를 사용하므로 단순 arity로
판정하지 않는다. 각 호출의 두 번째 인자가 자동 연결 타입이고 세 번째 인자가
channel name인지 확인한다.

최종 보고에는 아래 내용을 반드시 남긴다.

- draft spec 미반영 항목 리뷰 결과
- POSD 리팩토링 반복 결과
- core 전체 테스트 결과
- 바인딩별 테스트 결과
- sample smoke 결과
- single perf smoke 결과
- multi perf smoke 결과
- native runtime 갱신 파일 목록
- 정식 문서 갱신 파일 목록

아래 세 문장이 모두 참일 때만 완료다.

```text
미반영 항목이 없습니다.
POSD 리팩토링 후보가 없습니다.
전체 테스트와 sample/perf smoke가 모두 통과했습니다.
```

## 15. 최종 증거

이 절은 2026-05-02 최종 구현과 native runtime 갱신 뒤의 검증 결과를 남긴다.

### 15.1 spec 반영 리뷰

draft spec 대비 미반영 항목이 없습니다.

- `core/include/zlink.h`, `core/include/zlink_enum.h`에 새
  `zlink_auto_connect_type_t`, Discovery 생성자, Registry channel 조회 계약,
  service/member/topology 구조체 변경이 반영되었다.
- Discovery/Registry/Spot runtime은 channel contract와 자동 연결 타입을 기준으로
  role 검증, pairwise initiator, conflict winner, loser projection 제거,
  lifecycle 정리를 수행한다.
- `zlink_service_type_t`, dealer peer mode enum/API는 공개 API, 바인딩 API,
  sample/perf 코드에서 제거되었다. 정식 문서에는 제거 사실을 설명하는 migration
  note만 남아 있다.
- stale API 감사:

```bash
rg -n "zlink_service_type_t|ZLINK_SERVICE_TYPE|ServiceType|service_type|DiscoveryDealerPeerMode|zlink_discovery_set_dealer_peer_mode|setDealerPeerMode|set_dealer_peer_mode|discovery_dealer_peer_mode" \
  core bindings samples doc/spec doc/guide doc/internals doc/site/docs \
  --glob '!**/build/**' --glob '!**/target/**' --glob '!**/node_modules/**' \
  --glob '!doc/spec/draft/**' --glob '!doc/plan/**' \
  --glob '!bindings/**/doc/plan/**' --glob '!bindings/**/docs/plan/**' \
  --glob '!bindings/c/bench/**' --glob '!core/external/**'
```

결과는 제거 note 4건과 제거 API 부재를 검증하는 바인딩 테스트만 남았다.

### 15.2 POSD 리팩토링 결과

의미 있는 POSD 리팩토링 후보가 없습니다.

최종 리팩토링에서 아래 위험 신호를 해소했다.

- Registry peer sync conflict 처리의 seq 검증과 projection 갱신을 한 임계 영역으로
  모아 시간적 분해를 줄였다.
- `discovery_t::service_name()` alias와 discovery topology의 오래된
  `service_name` key를 제거해 channel naming 정보 중복을 줄였다.
- channel contract의 unused conflict flag를 제거했다.
- Spot discovery 시나리오와 binding sample의 readiness 판정을 실제 전달 조건에
  가깝게 정리했다.

### 15.3 core 검증

```bash
cmake --build core/build -j"$(nproc)"
cmake --build bindings/c/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -j"$(nproc)"
./core/tests/run_test_lanes.sh --include-e2e
```

결과:

- core build 통과
- C binding build 통과
- core `ctest`: 100/100 통과
- test lanes: unittest 20/20, integration 62/62, e2e 2/2 통과

### 15.4 binding test/sample/perf smoke

native runtime을 갱신한 뒤 최종 smoke 결과는 아래와 같다.

| 바인딩 | tests | samples/examples | single perf | multi perf |
|--------|-------|------------------|-------------|------------|
| C | 해당 없음 | samples 10/10 | 6개 pattern 통과 | 8개 pattern 통과 |
| C++ | tests 8/8 | samples 11/11 | 6개 pattern 통과 | 지원 6개 통과, `MULTI_SPOT` unsupported, `SPOT_SENDSEND` 미구현 |
| .NET | tests 136/136 | samples 11/11 | 6개 pattern 통과 | 지원 4개 통과, `MULTI_ROUTER_ROUTER`/`MULTI_SPOT` unsupported, `SPOT_SENDSEND` 미구현 |
| Go | `go test ./...` 통과 | samples 11/11 | 6개 pattern 통과 | 지원 7개 통과, `SPOT_SENDSEND` 미구현 |
| Java | Gradle tests 통과 | samples 11/11 | 6개 pattern 통과 | 지원 7개 중 6개 기본 통과, `MULTI_SPOT`은 `--clients 2` 통과, `SPOT_SENDSEND` 미구현 |
| Node | build/typecheck/native rebuild/tests 통과 | samples 11/11 | 6개 pattern 통과 | 지원 7개 통과, `SPOT_SENDSEND` 미구현 |
| Python | pytest 55 passed, 10 skipped | examples 11/11, samples 11/11 | 6개 pattern 통과 | 지원 7개 통과, `SPOT_SENDSEND` 미구현 |
| Rust | tests 10/10 | samples 11/11 | 6개 pattern 통과 | 지원 7개 중 6개 기본 통과, `MULTI_SPOT`은 `--clients 2` 통과, `SPOT_SENDSEND` 미구현 |

single perf 6개 pattern은 `PAIR`, `PUBSUB`, `DEALER_DEALER`,
`DEALER_ROUTER`, `ROUTER_ROUTER`, `SPOT`이다. multi perf 기본 목표는
`DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `SPOT`,
`SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM`이지만, 일부 언어 runner에는
`SPOT_SENDSEND` 구현이 없다. runner가 unsupported 또는 미구현으로 보고한
pattern은 실패가 아니라 해당 바인딩의 현재 perf surface 밖으로 분류했다.
Go/Java/Python/Rust의 일부 multi SPOT smoke는 기본 client 수에서 resource나
ready timing이 불안정해 smoke 목적에 맞게 `--clients 2` 또는 `--clients 10`으로
개별 재실행했다. 같은 자동 연결 API, pattern, transport, message size를 검증한다.

### 15.5 native runtime 갱신

최신 core/C binding 산출물로 아래 runtime set을 갱신했다.

- `bindings/cpp/native/linux-x86_64/libzlink.so.5.3.4`
- `bindings/dotnet/native/linux-x86_64/libzlink.so.5.3.4`
- `bindings/dotnet/runtimes/linux-x64/native/libzlink.so.5.3.4`
- `bindings/go/native/linux-x86_64/libzlink.so.5.3.4`
- `bindings/java/native/linux-x86_64/libzlink.so.5.3.4`
- `bindings/java/src/main/resources/native/linux-x86_64/libzlink.so.5.3.4`
- `bindings/node/native/linux-x86_64/libzlink.so.5.3.4`
- `bindings/node/prebuilds/linux-x64/libzlink.so.5.3.4`
- `bindings/python/src/zlink/native/linux-x86_64/libzlink.so.5.3.4`
- `bindings/rust/native/linux-x86_64/libzlink.so.5.3.4`

### 15.6 문서 반영

정식 문서는 `doc/spec/core/service/*`, `doc/guide/07-*`,
`doc/internals/*`, `doc/spec/bindings/*`, `doc/site/docs/api/*`,
`doc/site/docs/guide/*`, `doc/site/docs/internals/*`에 반영했다.

```text
미반영 항목이 없습니다.
POSD 리팩토링 후보가 없습니다.
전체 테스트와 sample/perf smoke가 모두 통과했습니다.
```
