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
| JV-DOC-003 | `spring-boot-actor-session.ko.md` | actor/session 등록·동작 | 검토 완료 |
| JV-DOC-004 | `spring-boot-channel-messaging.ko.md` | channel/route/fanout | 검토 완료 |
| JV-DOC-005 | `spring-boot-monitoring.ko.md` | monitoring/flow/metrics/drain | 검토 완료 |
| JV-DOC-006 | `spring-boot-registry.ko.md` | location store/runtime | 검토 완료 |
| JV-DOC-007 | `spring-boot-spot.ko.md` | Spot/actor lifecycle와 messaging | 검토 완료 |
| JV-DOC-008 | `spring-boot-stream.ko.md` | server STREAM/session | 검토 완료 |
| JV-DOC-009 | `stream-connector.ko.md` | client connector 전체 계약 | 검토 완료 |
| JV-DOC-010 | `stage-wrapper-on-spot.ko.md` | 상위 guide; interface 분모 비적용, G7 정합성 검토 | 검토 완료 |

공통 spec 19개는 모든 행의 동작 근거로 함께 적용한다. E2E 문서는 새 public API의 근거가
아니며, 정식 계약을 검증할 scenario와 누락을 식별하는 데만 사용한다.

## 3. symbol·동작·test gap

| ID | 계약 영역 | 현재 증거 | 필요한 구현·삭제 | 먼저 고정할 검증 | 상태 |
|----|-----------|-----------|------------------|------------------|------|
| JV-G0-ASYNC-001 | handler/lifecycle/factory `CompletionStage` 완료 | channel, route, Spot handler와 actor factory가 값/void 직접 반환 | 모든 application callback을 stage 반환으로 바꾸고 serial turn이 stage 완료까지 유지되게 한다 | exact return reflection, blocked-stage same-owner ordering | GAP |
| JV-G0-ASYNC-002 | one-way `void submit()`, 단일 비동기 terminator | `ZLinkSubmitStage`, public `await/yield`, duplicated join terminator | completion object와 blocking/legacy terminator 삭제 | exported symbol no-hit, void return과 bounded admission | GAP |
| JV-G0-CANCEL-001 | Java public API에 framework cancellation token 없음 | public `CancellationToken`, context property와 lifecycle/transfer 인자 | public token type/member/parameter 삭제; shutdown cancellation은 runtime 내부 소유 | JAR/reflection absence와 host shutdown | GAP |
| JV-G0-ACTOR-001 | nullable Spot 식별자, 단일 join call, sealed 결과 | `isJoined`, `getSpot`, request-less default join throw와 독립 결과 필드 | 중복/impossible state 삭제, request-bearing member와 Accepted/Rejected 결과 구현 | absence, sealed exhaustiveness, implementor compile fixture | GAP |
| JV-G0-SPOTHANDLE-001 | opaque `SpotHandle`과 두 resolver | `SpotRemoteRef*`가 transport 주소를 public으로 노출 | legacy ref 삭제, snapshot/watch/refresh와 request 1회 안전 retry 구현 | legacy absence, initial/refresh/watch/poll/movement | GAP |
| JV-G0-DISPATCH-001 | dispatch 최적화 은닉과 typed identity 단일 결정 | public `ZLinkDispatchMode`, typed call `packetName` override | mode와 typed override 삭제; raw encoded path만 명시 identity 유지 | export/method absence와 descriptor identity | GAP |
| JV-G0-SPOT-001 | Spot registry/close와 request create overload | manager/context member 누락 | 정식 member와 lifecycle ordering 구현 | exact reflection, request/reply/close/timer ordering | GAP |
| JV-G0-RUNTIMEOPT-001 | route-mesh options와 공통 endpoint handle | route-mesh facade 없음, nominal endpoint shape 반복 | `ZLinkEndpointConnections` 재사용, live/restart/location weight 정합 구현 | exact facade, add/remove/snapshot/replay/weight row | GAP |
| JV-G0-SESSION-001 | typed application session handler, raw framework dispatcher | typed handler가 raw를 상속하고 default UOE를 던짐 | raw application bridge 삭제, type descriptor와 JSON default dispatch 구현 | exact hierarchy, codec/ordering/backpressure/lifecycle | GAP |
| JV-G0-OBS-001 | sealed monitoring, flow, metrics, drain, session closing | location source/typed event와 운영 계약 다수 누락; Redis fixture가 `Draining` 누락으로 실패 | 정식 type/catalog/flow wire/drain phase/close reason 전체 구현 | export inventory, UUIDv7, meter catalog, shared drain, Config 11 | GAP |
| JV-G0-EXPORT-001 | application package에 public contract만 export | public `ZLinkBackend*`, `*BackendAdapter`, handler factory/suspend invoker | internal/package-private로 내리고 clean consumer에서 import 불가 보장 | JAR allowlist와 clean consumer compile | GAP |
| JV-G0-NAMING-001 | `CompletionStage` method에 불필요한 `Async` suffix 없음 | location/query/dispatcher/timer/connector에 `*Async` 공개 | 정식 Java 이름으로 rename하고 alias 삭제 | exported public method no-hit와 consumer compile | GAP |
| JV-G0-LOC-001 | sealed `ZLinkLocationKey`과 전체 store/runtime facets | sealed 통합 key는 이미 구현되어 target contract test가 통과하고, 나머지 facet/member와 동작 proof는 미완료 | 누락 interface/member와 owner/generation/page/watch/readiness 동작 구현 | reflection과 monotonicity/paging/watch/readiness | GAP |
| JV-G0-CONNECTOR-001 | Java Stream Connector 정식 전체 계약 | lifecycle/call/codec/observer/TLS/package coverage 미완전 | `stream-connector.ko.md` 전체 member와 동작 정렬 | JAR snapshot과 §13 contract tests | GAP |
| JV-G0-REUSE-001 | Kotlin coroutine invocation의 단일 owner | Java core가 reflection/private Kotlin runtime fallback을 재구현하고 Kotlin module provider와 중복 | core fallback 삭제, provider 부재 시 configuration error; Kotlin module만 invocation 소유 | fallback symbol/reflection no-hit와 provider/no-provider test | GAP |

## 4. E2E·sample inventory

| ID | 영역 | 현재 차이 | 필요한 작업 | 상태 |
|----|------|-----------|-------------|------|
| JV-G0-E2E-001 | Config 11 | `ObservabilityOps`와 OBS-A1~C5 13개 전체 누락 | 정식 계약 public surface만 사용하는 suite/runner/marker 구현 | GAP |
| JV-G0-E2E-002 | Config 8 | `YieldDispatch`와 `YD-*` 구형 suite/ID | `AutomaticTurnDispatch`, ATD-A1~E5 selector로 이관 | GAP |
| JV-G0-E2E-003 | Config 8 shutdown | E3가 opt-in 환경변수일 때만 실행 | 기본 `all`에서 bounded shutdown scenario 실행 | GAP |
| JV-G0-E2E-004 | Config 6 | `DiscoveryRegistryHa` 구형 suite/selector | `StoreFailure`, SF-A1~E1 사용성으로 이관 | GAP |
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
e3caac9f35e6e100b38280eccb9276074a48c79433fe634f4b86c754866deed6 common/README.ko.md
a9bebea7cfb3630f1e80813099d9207493143194d09a17924757778ca88504f4 common/actor-model.ko.md
13dcccaf3edba4f8ff3c9eaf6d058715ffcb1869922244832dd0a4908bc767cb common/async-execution-policy.ko.md
97732ddecfd56b39dd20b213dc76103d24d8e9111fb9da5db5998e1dc0018b92 common/channel-topology.ko.md
f4db859ee16cb8ebeb28c3857bb020fdfd441a569da684352aff86d96a7d91e9 common/flow-correlation.ko.md
57f32cfe04b47a1d0018c644fa3fb57d0ed56d8ec592721e86fbb9dc3e46e13c common/framework-api.ko.md
e015afd89bba3e4b36f74aadf409e4fea1f4f2bb932e57108cef4db3c67fd883 common/graceful-drain-handoff.ko.md
e60c8f193a3f8bad1814965e247be784a8e758bc753c6b157dfe8f568bb4f723 common/implementation-gap.ko.md
921010d790f79329605006181884f257b361a58f4f13d11740b27130c9a6e624 common/interaction-model.ko.md
e3e45861394c4f7e7c54687e63b68a17ed00b5dfa2db38c969bd3e2fd63f571d common/location-runtime.ko.md
4040f9d3042d1d3e4cdb3533f8d033157d218970c778586a2cbcde4488c704ee common/location-store-redis.ko.md
06dadcb858ffa2c9913dbd1931332b9dc321c7cd54ae56fd6ec4f95d803620f0 common/message-flow-tracing.ko.md
3ce18fbcfbda8d410cf3c6792dd697773a8b4b2dca7cb0ebb0d64a48a06e7133 common/message-model.ko.md
3fb43bd272188f83abc4ca0f7e06589b33d544f2658fe43e35d679fd39c99530 common/overview.ko.md
d28134e15d84211823dcdbf68df23137982622754904b7f333464e70b71ff0f5 common/public-contract-governance.ko.md
c215d647023a9b767fc048d2aa5c349ff4a3f5b49d27df97fbeaccf7aa9a214d common/runtime-metrics.ko.md
476c1dea292fd3d9240d04e33ad82e80f6fe004ac165b835866819679494e9d8 common/session-actor-dispatch.ko.md
19fe9c09a731694e19d9160853e99096e8e4284ab616565ea905b4d9e1aa5ab5 common/spot-actor.ko.md
30827c93383fbcf0d65b607f5562091d7481504e1e907553f57efce8a84d6a82 common/spot-address-messaging.ko.md
23f91a4e7ded2ad7fea90261dc2ded56d79953481e61fa4b4bce3e0dc7cd2d42 java/README.ko.md
a1c26c914a562b2fe12c87e18604b24f09130e1a233a48e43986580de05003ba java/handler-interfaces.ko.md
907e40abf6081cc4fbfa5026c5691f4d6169b3eca0530723d42252ddcb5138c6 java/spring-boot-actor-session.ko.md
ea244a300f2b7d303a37aaa2dc4765ca479eba5fe9441f8e0b329caebcc6e3a8 java/spring-boot-channel-messaging.ko.md
705e8c2b9022383e1921f179d39c196117e5c13f7c8cbf5d8d6457d7bdcc390b java/spring-boot-monitoring.ko.md
5f9852e946a35953bc1c5ce59431b6aad40fa9a724b0182d581d6b0b1c035238 java/spring-boot-registry.ko.md
ab507e968b3a84b240ce15982a27599345e045d0c0db87408eb05c4653cd2249 java/spring-boot-spot.ko.md
703d825d8d42dcb5980b0d5ee9378ae41b44e6c742f52a87bbeab5b8db1821ba java/spring-boot-stream.ko.md
dcb8842fb03803da518e17f756c5c70853ea38233ea578f371b229d5341310fb java/stage-wrapper-on-spot.ko.md
db3c779d63244196ab031030e3e8b607c2b6a1912b345b14b9d7b5f4646e0290 java/stream-connector.ko.md
```

## 7. G0 완료 조건

- [x] 정식 문서 inventory와 hash 고정
- [x] public symbol/동작/package/E2E gap을 작업 ID로 등록
- [x] 삭제 목록 고정
- [x] bindings package version과 public-only 사용 감사
- [x] 각 GAP을 재현하는 active failing contract test: target 14개 중 12개 실패, 2개 도달
- [x] documentation regression이 정식 경로와 ledger hash를 fail-closed로 검증: hash test PASS,
  E2E inventory는 ATD 19개와 OBS 13개 누락을 정확히 실패
