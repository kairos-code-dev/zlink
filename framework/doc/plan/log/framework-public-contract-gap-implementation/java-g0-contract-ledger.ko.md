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
| JV-DOC-001 | `README.ko.md` | Java naming, 비동기 완료, 취소 정책 | 검토 완료 |
| JV-DOC-002 | `handler-interfaces.ko.md` | 전체 public interface와 §8.1 gap | 검토 완료 |
| JV-DOC-003 | `system-structure.ko.md` | actor/session 등록·동작 | 검토 완료 |
| JV-DOC-004 | `system-structure.ko.md` | channel/route/fanout | 검토 완료 |
| JV-DOC-005 | `system-structure.ko.md` | monitoring/flow/metrics/drain | 검토 완료 |
| JV-DOC-006 | `system-structure.ko.md` | location store/runtime | 검토 완료 |
| JV-DOC-007 | `system-structure.ko.md` | Spot/actor lifecycle와 messaging | 검토 완료 |
| JV-DOC-008 | `system-structure.ko.md` | server STREAM/session | 검토 완료 |
| JV-DOC-009 | `32-stream-connector.ko.md` | client connector 전체 계약 | 검토 완료 |
| JV-DOC-010 | `25-stage-wrapper-on-spot.ko.md` | 상위 guide; interface 분모 비적용, G7 정합성 검토 | 검토 완료 |

공통 spec 19개는 모든 행의 동작 근거로 함께 적용한다. E2E 문서는 새 public API의 근거가
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
| JV-G0-CONNECTOR-001 | Java Stream Connector 정식 전체 계약 | lifecycle/call/codec/observer/TLS/package coverage 미완전 | `32-stream-connector.ko.md` 전체 member와 동작 정렬 | JAR snapshot과 §13 contract tests | GAP |
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
a7e2944d4f73accc42e4efe35f7758048361ab6fb584ffaab61fbf5d3749950e common/README.ko.md
281afb2d758740012507ff6c9e3b84ff3eced8dab68aff622aa0e1e7b8accf55 common/22-actor-model.ko.md
7993d0aa2f5f1a286a19e55a05eb71bdbb14f44926967aa1f58cb7d22378de0c common/04-async-execution-policy.ko.md
e1187727d4d50ba22888a1c55194db8e1f5ffb5e5550d5f664612c700d988b2d common/11-channel-messaging.ko.md
630ab06dc54466ec0c54dc7fda8fe43ca7cf3901e488ecc527e591b010cc4f86 common/10-channel-topology.ko.md
3dbcd28d5e9bdd9f7a6e1473f9291eb14fde6a1e1622debf6f9c760754b942f8 common/53-flow-correlation.ko.md
4e2e7767a0a0ff2324108270f53b0ae51f8f9792991c8db4885ab5f4bdbe8636 common/05-framework-api.ko.md
1d415bb35d1614e428ca90d8b7ea3bacd8ee78520330c7aa393bfc7316b1f754 common/54-graceful-drain-handoff.ko.md
c9b8092393ec224d78e4da36ed3ad76d77a949ce90be500e3946cc6e2c1a12cd common/90-implementation-gap.ko.md
5abba134f48f8d95d63e8cc409de998ab146cdad39fdf7cba22f6455b19b046d common/02-interaction-model.ko.md
e5f0140d6f37cb592be91d989005983192705970efcf9f5e2defbd75083416a6 common/40-location-runtime.ko.md
253e1a9fdd6ab9041a4158f09c64b6a36e4a55d5d019b3627397c4870ff1f210 common/41-location-store-redis.ko.md
68b153f892d88b9f0ddab3d7f2fa266606f7b4d3527746172662166ff011144a common/52-message-flow-tracing.ko.md
c8477f874dbf0ff6aa8110708b9a5e7a1dc0b99ae387c79cbe06e1df8c0c9d2e common/03-message-model.ko.md
9bb03da9c4e7522c409597f63228b35ce23a8f5136e965d2ce160323cf65599a common/01-overview.ko.md
67c50c2750243eaaaad34dd579d45707dd37eacca9c31d3c074601ce3692bd88 common/00-public-contract-governance.ko.md
d808af4314ff9e1a3531275310a2aa2325403c74f362fb637da60c35937653dd common/51-runtime-metrics.ko.md
946c4ad376b143b56b13b3585be7e51e82abf7ce2c973645803cd9c2ff04b441 common/50-runtime-monitoring.ko.md
de2e46e5176dc6257eb4d53996bcbac99ce93f538d4bfb32cf6d84731e5bb1a8 common/31-session-actor-dispatch.ko.md
a4e8e2231abb2ecbb70e3c1938bb5b9bf39233981e329a25f8dc5d8befa407b2 common/23-spot-actor.ko.md
5c3b233e111d37d4089969b8ba317f7d5525dce6adcab6d8bdd6fe9b3b072c9d common/24-spot-address-messaging.ko.md
81577b4a05ee84d383b628a6ef9cdf21dda04dbe1abce1887084775ede6caf3a common/20-spot-messaging.ko.md
78166bc536c5b1de58ce738f905ec86bd1d70df25bffcab01862026b2e3f0067 common/21-spot-node.ko.md
d837409648b996bf010ac1c9509f24d3679d58948006c121eff61963ddc01c3c common/25-stage-wrapper-on-spot.ko.md
01b72e31d4f661c60cf70110e53d2d158b68d5ce18c270f4fdc033adc083da05 common/32-stream-connector.ko.md
cc48b71a526cf8c4da872eb12d1f87df927f29aed3cc9af47aff398e7570fbc5 common/30-stream-session.ko.md
ec90bd78db9467e2d1f0e8a7a78a6b5246927ab5f165f67e65f44a9014813ee2 java/README.ko.md
9ebc0cc7dda6617507bc635bd6cbbcd67215e15109ca896f9e7efdf4df022a27 java/02-handler-interfaces.ko.md
90f185d93bc93af527ccc9d40a192ee1e1500c1622fe3080c9374f43fb4bb8ea java/01-system-structure.ko.md
470e3abcad0d7c8f07b7610176924de8110a5507fc839c3c846e4b30b39ed306 java/03-stream-connector.ko.md
```

## 7. G0 완료 조건

- [x] 정식 문서 inventory와 hash 고정
- [x] public symbol/동작/package/E2E gap을 작업 ID로 등록
- [x] 삭제 목록 고정
- [x] bindings package version과 public-only 사용 감사
- [x] 각 GAP을 재현하는 active failing contract test: target 14개 중 12개 실패, 2개 도달
- [x] documentation regression이 정식 경로와 ledger hash를 fail-closed로 검증: hash test PASS,
  E2E inventory는 ATD 19개와 OBS 13개 누락을 정확히 실패
