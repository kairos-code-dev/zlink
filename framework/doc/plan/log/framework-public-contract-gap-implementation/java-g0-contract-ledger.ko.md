# Java G0 공개 계약 및 구현 gap ledger

이 문서는 Java 정식 공개 계약의 규범 항목을 실제 public symbol, 구현 동작, 실패 test와
연결한다. 정식 계약은 현재 구현의 최소 공통분모로 축소하지 않는다. 현재 상태가 `GAP`인
행은 G1 이후 구현·삭제·검증 작업의 입력이며, 호환 alias를 남기지 않는다.

## 1. 기준

- 기준일: 2026-07-12
- bindings dependency: `systems.zlink:zlink:9.0.0`
- production 분모: Java framework main source 546개
- 정식 언어 문서: 10개
- 공통 E2E 분모: Config 1~11, scenario 181개
- baseline 명령: `./gradlew --no-daemon test contractTest fakeBackendTest integrationTest sampleTest`
- baseline 결과: exit 1. Redis actor-location-v2 fixture의 peer row가 요구하는
  `Draining:false`를 Java serializer가 누락했다.
- public contract 변경은 정식 Java spec의 시그니처를 그대로 구현하며 deprecated/legacy
  adapter를 정식 package에 남기지 않는다.

## 2. 계약 coverage

| ID | 정식 계약 문서 | 적용 범위 | 상태 |
|----|----------------|-----------|------|
| JV-DOC-001 | `README.ko.md` | 언어별 계약 index와 취소 표현 | 검토 완료 |
| JV-DOC-002 | `01-system-structure.ko.md` | 패키지 구조·배포, Spring Boot 등록·부트스트랩·lifecycle — channel · SPOT · STREAM · actor session · monitoring 등록 표면과 startup validation | 검토 완료 |
| JV-DOC-003 | `02-handler-interfaces.ko.md` | 전체 public interface·annotation·context·options 카탈로그 | 검토 완료 |
| JV-DOC-004 | `03-stream-connector.ko.md` | client connector의 public 표면 | 검토 완료 |
| JV-DOC-010 | `25-stage-wrapper-on-spot.ko.md` | 상위 guide; interface 분모 비적용, G7 정합성 검토 | 검토 완료 |

공통 spec 전체(§6 snapshot이 고정한 목록)는 모든 행의 동작 근거로 함께 적용한다. E2E 문서는 새 public API의 근거가
아니며, 정식 계약을 검증할 scenario와 누락을 식별하는 데만 사용한다.

## 3. symbol·동작·test gap

| ID | 계약 영역 | 현재 증거 | 필요한 구현·삭제 | 먼저 고정할 검증 | 상태 |
|----|-----------|-----------|------------------|------------------|------|
| JV-G0-ASYNC-001 | handler/lifecycle/factory `CompletionStage` 완료 | 정식 stage 반환과 automatic-turn entry ordering 구현 | 없음 | target contract PASS, Config 8 ATD-A1~E5 PASS | DONE |
| JV-G0-ASYNC-002 | one-way `void submit()`, 단일 비동기 terminator | one-way `void submit`; legacy completion/await/yield production symbol 없음 | 없음 | target contract와 production grep PASS | DONE |
| JV-G0-CANCEL-001 | Java public API에 framework cancellation token 없음 | production public token symbol 없음 | 없음 | target contract와 production grep PASS | DONE |
| JV-G0-ACTOR-001 | nullable Spot 식별자, 단일 join call, sealed 결과 | `spotRid`, 단일 join call과 sealed 결과 구현; legacy getter 없음 | 없음 | target contract와 fake backend actor tests PASS | DONE |
| JV-G0-SPOTHANDLE-001 | opaque `SpotHandle`과 두 resolver | opaque handle과 resolver 구현; legacy remote ref production symbol 없음 | movement와 retry 전체 E2E는 별도 gate에서 확인 | exact target contract PASS | PARTIAL |
| JV-G0-DISPATCH-001 | dispatch 최적화 은닉과 typed identity 단일 결정 | public mode와 typed override 제거 | 없음 | target contract와 typed fake dispatch PASS | DONE |
| JV-G0-SPOT-001 | Spot registry/close와 request create overload | 정식 manager/context member 구현 | 전체 sample 재검증은 별도 gate | exact target contract와 fake backend PASS | PARTIAL |
| JV-G0-RUNTIMEOPT-001 | route-mesh options와 공통 endpoint handle | exact facade와 공통 endpoint 계약 구현 | restart/replay/weight 전체 E2E는 별도 gate | target contract PASS | PARTIAL |
| JV-G0-SESSION-001 | typed application session handler, raw framework dispatcher | typed hierarchy와 JSON 기본 dispatch 구현 | 없음 | target contract와 fake backend 86/86 PASS | DONE |
| JV-G0-OBS-001 | sealed monitoring, flow, metrics, drain, session closing | target public declaration 구현 | Config 11과 전체 운영 E2E는 이 갱신에서 미검증 | target contract PASS, 완료된 Config 6·8만 증거 | PARTIAL |
| JV-G0-EXPORT-001 | application package에 public contract만 export | backend와 handler runtime SPI가 internal 경계에 있음 | clean packaged consumer는 별도 gate | target contract PASS | PARTIAL |
| JV-G0-NAMING-001 | `CompletionStage` method에 불필요한 `Async` suffix 없음 | application 공개 계약의 legacy suffix 제거 | connector transport internal 이름은 public application 계약과 분리 유지 | target contract PASS | DONE |
| JV-G0-LOC-001 | sealed `ZLinkLocationKey`과 전체 store/runtime facets | exact facet 구현과 real Redis 장애·복구 검증 완료 | 없음 | target contract PASS, Config 6 SF-A1~E1 PASS | DONE |
| JV-G0-CONNECTOR-001 | Java Stream Connector 정식 전체 계약 | lifecycle/call/codec/observer/TLS/package coverage 미완전 | `03-stream-connector.ko.md` 전체 member와 동작 정렬 | JAR snapshot과 §13 contract tests | GAP |
| JV-G0-REUSE-001 | Kotlin coroutine invocation의 단일 owner | Java core fallback 제거, Kotlin module provider만 소유 | Kotlin provider 동작은 Kotlin gate에서 검증 | Java target contract PASS | PARTIAL |

## 4. E2E·sample inventory

| ID | 영역 | 현재 차이 | 필요한 작업 | 상태 |
|----|------|-----------|-------------|------|
| JV-G0-E2E-001 | Config 11 | `ObservabilityOps`와 OBS-A1~C5 13개 전체 누락 | 정식 계약 public surface만 사용하는 suite/runner/marker 구현 | GAP |
| JV-G0-E2E-002 | Config 8 | `AutomaticTurnDispatch`, ATD-A1~E5 정식 suite와 marker report 구현 | 없음 | DONE |
| JV-G0-E2E-003 | Config 8 shutdown | ATD-E3가 기본 `all` marker report에 포함됨 | 없음 | DONE |
| JV-G0-E2E-004 | Config 6 | `StoreFailure`, SF-A1~E1 정식 suite로 이관하고 real Redis 장애·복구 전체 통과 | 없음 | DONE |
| JV-G0-E2E-005 | Config 9 | focused selector가 무시되고 항상 전체 실행 | TA-A1~B3 selector를 client/runner까지 전달 | GAP |
| JV-G0-E2E-006 | Config 2 | 공통 계약 밖 `SM-Q9`가 기본 all에 포함 | 정식 ID에 통합하거나 공통 gate 밖 별도 실행으로 분리 | GAP |
| JV-G0-E2E-007 | Config 10 | 20개 ID runner는 있으나 feature-map/inventory 없음 | ID별 fixture·marker·계약 근거 문서화 | GAP |

Java sample manifest는 TicTacToe, Bingo, DeliveryDispatch, GameQuest, ShoppingMall,
SupportChat 여섯 sample을 모두 포함한다. 실행 성공은 G5에서 새로 검증한다.

## 5. 삭제 목록

`CancellationToken`, `ZLinkSubmitStage`, public `await/yield`, `ZLinkDispatchMode`와
mode property, `SpotRemoteRef*`, `isJoined`, `getSpot`, request-less/default-throw join,
중복 Spot/Entry join call, typed `packetName` override, raw typed-session bridge,
application-visible backend/adapter/handler factory/suspend invoker, obsolete `*Async` alias를
호환 계층 없이 삭제한다.

## 6. 문서 snapshot

```text
8ffae3ae36f3305e1dfa35d1874a1c2c9c57342f5f2116abbe7f5e432f79f595 README.ko.md
8cf0cac1e46c6086de082d8ad4aeae51f339245d05da1b7bc6175f9b622ec79e server/22-actor-model.ko.md
6614f5efd549442f95ac4f67f8ff1e10bba9c7061ee63a7608ffd91f43fea4bd 04-async-execution-policy.ko.md
5f190e3b4f1b93d4a0e03c9ba23b625a1b8a56c5d79dc361917215be73fa0839 server/10-channel-topology.ko.md
077319afac1aec1aba884853cd172443f5e2563d664b00b0a9e2468a252a196c server/53-flow-correlation.ko.md
06f3d56438301a80afd983475e58c65d3b0e678a32b832c5f13813bf937ffcb6 05-framework-api.ko.md
822ada32199d71d2c4505c561fc4f2f4db6f9c50d49eb2469b202d87dd2bc97f server/54-graceful-drain-handoff.ko.md
257c63fe8b9159a7b37cf335836e8b076c94db3ea22bae0abff69d1d8f65d698 90-implementation-gap.ko.md
df441c4de567865658b0b79ded6c840d020ccf60865f58e7990a248e9fa361a0 02-interaction-model.ko.md
dfa08a0db46f59bcd107347c9f02256ff023d7c64c3f8caac42772c37d7b058b server/40-location-runtime.ko.md
f84d4a035cd773d6fe8aa0096151909e92be0743b57445dd51e5b38eeab9376c server/41-location-store-redis.ko.md
0635851f5d9b3cf0fa6f481fb886200e1802f3bda6fe80db3648b35b53e22108 server/52-message-flow-tracing.ko.md
a165665cbb47ef2b69744cfa7614d40c35274154af47439693f811080934f914 03-message-model.ko.md
136b4b2378c404b4728a4e526f985da6303456c294c06e9e425a39abb99d816b 01-overview.ko.md
883e767f3c2b673c9dabc4083fa42a7fc29799d25ef9ad04761d9cbdbc5cb245 00-public-contract-governance.ko.md
d34e9b26860a2ee285b340c5234bb27fc4c82438bbdf375e697f1350a0c1ef1f server/51-runtime-metrics.ko.md
d30ea2acfbee45009ee2e0d000f2b37009ccf9f5f134c8ad29dd2035e3b8ab99 server/50-runtime-monitoring.ko.md
49f5154412ed827496ba50f2e49a0f6bc84f3e1bcfdb4022b561dbded9b64147 server/31-session-actor-dispatch.ko.md
ae0c25c9f67cb397da861e82d8aaf1311472dfe5e28212a88f1e0aa32ec20998 server/23-spot-actor.ko.md
45576c26b8061e0a1965d219d539080a4917c6c06572b5d63bccddfb2f1bbe4d server/24-spot-address-messaging.ko.md
d9546cf37a3f9f34e863ac4a63eda2e2af6f1985269279579fa5b53632978108 server/20-spot-messaging.ko.md
d9a36ee80739f7035a0371c703871cea26f952e353d0f123e45b69e44c540088 server/21-spot-node.ko.md
fe9072b34809ccc20b489f6a3ebdd093fdd35470d3f1ee291e065f5644cc5f99 stream-connector/32-stream-connector.ko.md
623bca5e070513cc314c2d7f93d00dcdeab8b5f473bdeb883bfb5711eaa028e0 server/30-stream-session.ko.md
d071c34ff8921f58e7ae16426c0536b8fec77983aee733b57ba846e856409c69 server/languages/java/README.ko.md
4cda8c55342623eb30ecb11e986c60985397acdf0303c0399356901c99ec30ba server/languages/java/02-handler-interfaces.ko.md
5c1d649623fc2836b25f78b231e833e0f9cbe37e4a1e8158a71642fac7b534d1 server/languages/java/01-system-structure.ko.md
a6a95b38c3a1f2c5ee38677d44ef3f5a4a31fb43c8d9ee4b8ff5653411daacd3 stream-connector/languages/java/03-stream-connector.ko.md
54f7a53bc1ff7cc97ada0a41d28f50678e43d68c3ad46e0beef43466dd8ccf5c server/25-stage-wrapper-on-spot.ko.md
7a1a32c29bc2cfc642ce465f71e5f405741a2a8c23b95d0426b132947fdd0202 server/11-channel-messaging.ko.md
```

## 7. G0 완료 조건

- [x] 정식 문서 inventory와 hash 고정
- [x] public symbol/동작/package/E2E gap을 작업 ID로 등록
- [x] 삭제 목록 고정
- [x] bindings package version과 public-only 사용 감사
- [x] 각 GAP을 재현하는 active failing contract test: target 14개 중 12개 실패, 2개 도달
- [x] documentation regression이 정식 경로와 ledger hash를 fail-closed로 검증: hash test PASS,
  E2E inventory는 ATD 19개와 OBS 13개 누락을 정확히 실패
