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
| JV-DOC-009 | `stream-connector.ko.md` | client connector 전체 계약 | 검토 완료 |
| JV-DOC-010 | `stage-wrapper-on-spot.ko.md` | 상위 guide; interface 분모 비적용, G7 정합성 검토 | 검토 완료 |

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
| JV-G0-CONNECTOR-001 | Java Stream Connector 정식 전체 계약 | lifecycle/call/codec/observer/TLS/package coverage 미완전 | `stream-connector.ko.md` 전체 member와 동작 정렬 | JAR snapshot과 §13 contract tests | GAP |
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
5146fa570e974fcf8cdfe02d7279f9a2826f86116dec7a50350314b181770401 common/README.ko.md
3d74929d5ea0817f3768bf7f79986cff8fb929f9882d989b4eccc160a1aeb60e common/actor-model.ko.md
125d7c6b9546f24456f4213c7d071b6d111749c6bd506f0426dbea6cbe222ec5 common/async-execution-policy.ko.md
8f262980732bf14b3b361f1e02aabd6719b3be881240669738cb0f59f6114c74 common/channel-messaging.ko.md
97732ddecfd56b39dd20b213dc76103d24d8e9111fb9da5db5998e1dc0018b92 common/channel-topology.ko.md
000bd7d758c0b133d3bad35311891e72403d1fbd905c55b12edf4390cd2b2dab common/flow-correlation.ko.md
96d97a95e3d8d021e4bbf50e8ddd3e0b251cd8faed6226c0a68e272fcd5ec9b2 common/framework-api.ko.md
e015afd89bba3e4b36f74aadf409e4fea1f4f2bb932e57108cef4db3c67fd883 common/graceful-drain-handoff.ko.md
944cf15bf6c806f337dcf1af208dff8f10e44695b6ea5cba20e8dad3fc6cd12d common/implementation-gap.ko.md
921010d790f79329605006181884f257b361a58f4f13d11740b27130c9a6e624 common/interaction-model.ko.md
e3e45861394c4f7e7c54687e63b68a17ed00b5dfa2db38c969bd3e2fd63f571d common/location-runtime.ko.md
4040f9d3042d1d3e4cdb3533f8d033157d218970c778586a2cbcde4488c704ee common/location-store-redis.ko.md
06dadcb858ffa2c9913dbd1931332b9dc321c7cd54ae56fd6ec4f95d803620f0 common/message-flow-tracing.ko.md
3ce18fbcfbda8d410cf3c6792dd697773a8b4b2dca7cb0ebb0d64a48a06e7133 common/message-model.ko.md
3fb43bd272188f83abc4ca0f7e06589b33d544f2658fe43e35d679fd39c99530 common/overview.ko.md
d28134e15d84211823dcdbf68df23137982622754904b7f333464e70b71ff0f5 common/public-contract-governance.ko.md
c215d647023a9b767fc048d2aa5c349ff4a3f5b49d27df97fbeaccf7aa9a214d common/runtime-metrics.ko.md
fe9136f1bd209e27ed60d85fe01c34a1df9e997917d85f598a7a99017332f218 common/runtime-monitoring.ko.md
3f6c88e95eefc17ebadc1559b71b62c77f4bcd5a68d9bc84db88ca7e067a71d5 common/session-actor-dispatch.ko.md
19fe9c09a731694e19d9160853e99096e8e4284ab616565ea905b4d9e1aa5ab5 common/spot-actor.ko.md
30827c93383fbcf0d65b607f5562091d7481504e1e907553f57efce8a84d6a82 common/spot-address-messaging.ko.md
a819760bb71acbdb06a4290b4a293c73c9000e0ec7f5af91835313cb45d531c3 common/spot-messaging.ko.md
a8efeed1b57db9dc0a03dc72ffdf15974b4cb6e7d7e8824c02f403f49f30a43c common/spot-node.ko.md
5819de40f22cdf8b33822620b590af442e2b77687c1c46a13daf1ab1073979d0 common/stage-wrapper-on-spot.ko.md
5f7c69ac0db66dbb33244e2791b18a4d00c9e079c33868e39949b5613c680c51 common/stream-connector.ko.md
433b31fd8b11d628e0cfa8f60dfae057d7c0f78c9466dd021ef7d7b9f81d1c7c common/stream-session.ko.md
dd55303b795f84f4c156f4effb2eeb44a70d27987b36d2216c073a4a1f296d75 java/README.ko.md
452915e0f12291ae505aa1e14ca5fe7f4316430488b9dd54135a81a0b989fb32 java/handler-interfaces.ko.md
9854cacab20fe2dca5415549083ff800f4aeda31d5863d8fa795399eb512e956 java/system-structure.ko.md
9854cacab20fe2dca5415549083ff800f4aeda31d5863d8fa795399eb512e956 java/system-structure.ko.md
9854cacab20fe2dca5415549083ff800f4aeda31d5863d8fa795399eb512e956 java/system-structure.ko.md
9854cacab20fe2dca5415549083ff800f4aeda31d5863d8fa795399eb512e956 java/system-structure.ko.md
9854cacab20fe2dca5415549083ff800f4aeda31d5863d8fa795399eb512e956 java/system-structure.ko.md
9854cacab20fe2dca5415549083ff800f4aeda31d5863d8fa795399eb512e956 java/system-structure.ko.md
f8e98419402ee8b914715f309500eb36006e8b60b7fbdedf693c60c622688c51 java/stream-connector.ko.md
```

## 7. G0 완료 조건

- [x] 정식 문서 inventory와 hash 고정
- [x] public symbol/동작/package/E2E gap을 작업 ID로 등록
- [x] 삭제 목록 고정
- [x] bindings package version과 public-only 사용 감사
- [x] 각 GAP을 재현하는 active failing contract test: target 14개 중 12개 실패, 2개 도달
- [x] documentation regression이 정식 경로와 ledger hash를 fail-closed로 검증: hash test PASS,
  E2E inventory는 ATD 19개와 OBS 13개 누락을 정확히 실패
