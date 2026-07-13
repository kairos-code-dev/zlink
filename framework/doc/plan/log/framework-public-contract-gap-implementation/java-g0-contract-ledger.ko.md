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
a7e2944d4f73accc42e4efe35f7758048361ab6fb584ffaab61fbf5d3749950e common/README.ko.md
6b68a42a1ebab714fc7b1cd775e45f4ab4adfdaf536d181fd44fccaa9f50e1fc common/22-actor-model.ko.md
9c8021112e2f4b1566679afeefaee8013b7dd643d12817260a063ec93cefb804 common/04-async-execution-policy.ko.md
a44902a9f8775cfabe50b946fe1cab7f1a56083ad4a210c13a7d941eaca31c41 common/10-channel-topology.ko.md
e897e8f98cdffb4f430f714ff6cf0cd47b1e7049cca9653f56fdb605d92a1696 common/53-flow-correlation.ko.md
a4b921089013fb418033e8f857951cf8786a43deb74e7c531b01f4833c3df417 common/05-framework-api.ko.md
4b9d03746a5601bc468f6df7c1c9995d5f761572f515f92e39eb0834c62a6602 common/54-graceful-drain-handoff.ko.md
bd2f0780ac03161373f2db834bbef426eb95963433667d4192ab6108f72cc65f common/90-implementation-gap.ko.md
bbc0dbf64aadd7b03e6b3952b2cfc283d69ce271968926c8fda1ce5a5d491492 common/02-interaction-model.ko.md
e5f0140d6f37cb592be91d989005983192705970efcf9f5e2defbd75083416a6 common/40-location-runtime.ko.md
253e1a9fdd6ab9041a4158f09c64b6a36e4a55d5d019b3627397c4870ff1f210 common/41-location-store-redis.ko.md
516df0e441d62169b57ae642d8e9301778c8d8e8ac7a849a9703bd7c02f6731e common/52-message-flow-tracing.ko.md
2a6cbc45740487b9fab970699b94cac29f774d397407d314747b5b94479b010c common/03-message-model.ko.md
729dabd5dfc131095164ec4dec823edb05a75d7ac3ac939ce67c109f9ba66274 common/01-overview.ko.md
883e767f3c2b673c9dabc4083fa42a7fc29799d25ef9ad04761d9cbdbc5cb245 common/00-public-contract-governance.ko.md
d808af4314ff9e1a3531275310a2aa2325403c74f362fb637da60c35937653dd common/51-runtime-metrics.ko.md
7c9fc83fc43202fae89864a8870b646ad788c7e49f85c4b3c5ec785274b49a94 common/50-runtime-monitoring.ko.md
4dac9b99fd195db97cf3f9bc375749311b7bbfac69e31749fbb203ad79a2ea8a common/31-session-actor-dispatch.ko.md
a4e8e2231abb2ecbb70e3c1938bb5b9bf39233981e329a25f8dc5d8befa407b2 common/23-spot-actor.ko.md
c4cb5fc6f41aea5877062956657668abfe7d228f0dca7b9e785eb0054e3e2353 common/24-spot-address-messaging.ko.md
34f0253beeafe0607da40b100ad78db40c0b018f3a18d3a60cffa66b31f80de4 common/20-spot-messaging.ko.md
03f4e3e114799a62cdd64cc230e11d5e8c024109747714db7306c000404552aa common/21-spot-node.ko.md
120235ca9f94d5e2431f79c2a618ff089d44ebc74698e0144a2f6429c3a1403d common/32-stream-connector.ko.md
fde5faaec066875870711a8f52d5fbfb543f80ddc102b83917eae3b6fc47b0fd common/30-stream-session.ko.md
ec90bd78db9467e2d1f0e8a7a78a6b5246927ab5f165f67e65f44a9014813ee2 java/README.ko.md
9ebc0cc7dda6617507bc635bd6cbbcd67215e15109ca896f9e7efdf4df022a27 java/02-handler-interfaces.ko.md
1d230aa7234f4ec96d5d49ebb69ec56cacb7692096b9da86df0e68c50d819ce2 java/01-system-structure.ko.md
2fb381cebcd0990332a37f4d0e872541f85e18492b2930bb9f3a2dc67150e8b3 java/03-stream-connector.ko.md
d837409648b996bf010ac1c9509f24d3679d58948006c121eff61963ddc01c3c common/25-stage-wrapper-on-spot.ko.md
7d372f6a15a0e45bf8eb9c26d891d02dca3bcb78475c0ab1925d34abbaefb91d common/11-channel-messaging.ko.md
```

## 7. G0 완료 조건

- [x] 정식 문서 inventory와 hash 고정
- [x] public symbol/동작/package/E2E gap을 작업 ID로 등록
- [x] 삭제 목록 고정
- [x] bindings package version과 public-only 사용 감사
- [x] 각 GAP을 재현하는 active failing contract test: target 14개 중 12개 실패, 2개 도달
- [x] documentation regression이 정식 경로와 ledger hash를 fail-closed로 검증: hash test PASS,
  E2E inventory는 ATD 19개와 OBS 13개 누락을 정확히 실패
