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
42d510ede11aa33269a48db8d21ad9d1f58b85845850f59c346964dbf939b101 common/22-actor-model.ko.md
a618c98fd558a77fcc106baf82d1f64aced55d2d4e011985857d2d2c8ab157b5 common/04-async-execution-policy.ko.md
c4bf1e04acc52e8f019e317efce655baef84e48ee6bca1f09dc35c175d0f7bf9 common/11-channel-messaging.ko.md
4edd7ceda0c0c5508c1af458c407e19305993e4e899fc22edde92a33d4a78200 common/10-channel-topology.ko.md
778755c8d0b278e18d691d0c7c9f9063dd9acf955eae7a5b809a96545715869f common/53-flow-correlation.ko.md
f5e5be70f886b986097f077a1f57876359808f458927b63e6d99bd07a5612a92 common/05-framework-api.ko.md
e1a42ee2323f58b3b0791c231caeba18c4edb5c3ea74e7880752b060e1381fba common/54-graceful-drain-handoff.ko.md
56ff5d3c45f15a2fdebbebf59a80d0937c8026712f79f32ee6969403d20a423b common/90-implementation-gap.ko.md
b6b54adc8eea5ab0aead4a65862f1e63ee9d944e1a3a22f880f278eeab31e253 common/02-interaction-model.ko.md
f9ac6e812cee636d79b8d6e4c0e099c9ce9b58ae27ba1f19d15fd5384a624980 common/40-location-runtime.ko.md
6ae2479eabdc71b1ec2d343664b3ce3d0ff9c99cc6b39d9ddeb9305757eca669 common/41-location-store-redis.ko.md
ce881d2eb8f921299016a0f8e87132d3458db69cb56706ce2b285de40da57722 common/52-message-flow-tracing.ko.md
0cf99c737691439d97e5ec8817061fda7244c72a289425a743bf031ebe9f2e54 common/03-message-model.ko.md
b11559025f3b4f8090429d70d0571d8f3676a46ed69be33f108001bc6026269d common/01-overview.ko.md
a991724444da99ad0e170cad87fcf7505d6b24293e220de6f457639110dd24cc common/00-public-contract-governance.ko.md
a0c9bdc1921e8bdc2f78d644b98f0fcdc8fb50bc56a49bffb52e387c503e6914 common/51-runtime-metrics.ko.md
e4b199a2f8019a2d93296f5b2cd9b98d3719b4c06cae5d6ecde527abca65b1f4 common/50-runtime-monitoring.ko.md
90f52f3745a0365b8a5eb220dd8d658e35c97a7d45c77908983618dedf35d847 common/31-session-actor-dispatch.ko.md
a0359fad886447347aa59182f1f6698ac91f5b78c0d682e3cef6ce3312dce6bd common/23-spot-actor.ko.md
be51bb1575529455e0a1ef7b163883b8bbb24dafd76de48f19ebe43a2c0213ba common/24-spot-address-messaging.ko.md
5b48a128626e30ceba413dddc86fa880bb070b65905df70a91bf0936d08dda09 common/20-spot-messaging.ko.md
cf41d54d59f8f5e02a9c459f5f6a6781c4bbf2445167a52c71e97e3187559fe2 common/21-spot-node.ko.md
09cc20c2f3d093724b4b5c292dcb4beefba8728ed706b1f5e84891a69cfa50d0 common/25-stage-wrapper-on-spot.ko.md
b96317b588160db9c87b031362d6b29a9b1c3f01d7c8a49b6e0494eed67e0744 common/32-stream-connector.ko.md
b599853acda9e675cf3f81d31cdc4fbae2322b722290e0ac2eaa76339c60d7d4 common/30-stream-session.ko.md
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
