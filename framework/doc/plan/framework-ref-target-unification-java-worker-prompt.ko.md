# Java Worker Prompt: ActorRef / SpotRef 전송 대상 통일

기준 문서: `framework/doc/plan/framework-ref-target-unification-plan.ko.md`

## 목표

Java framework public contract에서 메시징 대상 개념을 `ActorRef` / `SpotRef`로 통일한다.
actor id 또는 spot id만 받아서 메시지를 보내는 API는 제거한다. id는 조회 입력이고, ref는 전송
입력이다.

이 작업은 Java E2E/sample gap 제거 작업과 함께 진행한다. 기준 문서는
`framework/doc/plan/framework-java-e2e-sample-gap-closure-plan.ko.md`이다. Ref 대상 통일은 Java public
surface, E2E scenario, sample 코드의 호출 모양을 바꾸므로, 이름 변경과 API 제거만 끝내고 E2E/sample
gap을 남기면 완료로 보지 않는다.

## 통합 수행 범위

이 worker는 아래 두 흐름을 같은 작업 범위로 다룬다.

1. 이 문서의 `ActorRef` / `SpotRef` 전송 대상 통일을 구현한다.
2. `framework-java-e2e-sample-gap-closure-plan.ko.md`의 Java E2E/sample gap 제거 항목을 함께 닫는다.

진행 순서는 public contract 안정성을 우선한다.

1. 먼저 현재 Java E2E/sample inventory와 feature-map의 `gap`/`partial` 항목을 확인한다.
2. Ref 대상 통일 변경이 해당 항목의 구현 또는 호출 예제에 영향을 주는지 확인한다.
3. 영향을 받는 E2E와 sample은 새 `ActorRef` / `SpotRef` 표면으로 바로 갱신한다.
4. 이미 구현 가능한 Java E2E/sample gap은 같은 PR 범위에서 runner evidence까지 닫는다.
5. public API 설계가 아직 확정되지 않은 gap은 internal package, reflection, raw-frame 우회, 테스트 전용
   adapter로 숨기지 않는다. 필요한 spec/guide/draft 검토 항목으로 분리하고, 완료 조건에서는 blocked로
   남긴다.

특히 아래 영역은 ref 대상 통일과 gap 제거를 함께 확인한다.

- `framework/languages/java/e2e/SpotService`
- `framework/languages/java/e2e/YieldDispatch`
- `framework/languages/java/e2e/RuntimeMonitoring`
- `framework/languages/java/e2e/ToActorMessaging`
- `framework/languages/java/samples/java/Bingo`
- `framework/languages/java/samples/java/DeliveryDispatch`
- `framework/languages/java/samples/java/SupportChat`
- `framework/languages/java/samples/java/ShoppingMall`

Sample 코드는 사용자가 따라 할 public API 예시이므로, ref 통일 중에도 sample 안에 runtime/internal
package 접근, id-only 전송 우회, codec 수동 우회, 임시 helper를 넣지 않는다.

## Naming 규칙

| 현재 이름 | 최종 이름 |
|-----------|-----------|
| `ZLinkActorRef` | `ActorRef` |
| `ZLinkActorRefSnapshot` | `ActorRefSnapshot` |
| `ZLinkSpotAddress` | `SpotRef` |
| `ZLinkSpotAddressResolver` | `ZLinkSpotRefResolver` |

Manager, client, store, runtime, options, builder 같은 service/role 타입은 `ZLink` prefix를 유지한다.
값 개념인 ref 타입에는 `ZLink`를 붙이지 않는다.

## 제거 대상

```text
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorClient.java
  sendToActor(String actorId, Object message)
  requestToActor(String actorId, Object request)

framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/ZLinkSpotOutbound.java
  sendToSpot(RoutingId spotRid, Object message)
  requestToSpot(RoutingId spotRid, Object request)

framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/channels/ZLinkRouteClient.java
  sendToSpot(..., ZLinkSpotAddress address, ...)
  requestToSpot(..., ZLinkSpotAddress address, ...)
```

`ZLinkActorContext.joinSpot(RoutingId spotRid, ...)`는 actor join workflow이므로 이번 제거 대상이 아니다.
단, 일반 spot messaging API처럼 문서화하지 않는다.

## 추가/변경 대상

- `ZLinkActorRef`를 `ActorRef`로 변경한다.
- `ZLinkActorRefSnapshot`을 `ActorRefSnapshot`으로 변경한다.
- `ZLinkSpotAddress`를 `SpotRef`로 변경한다.
- `ZLinkSpotAddressResolver`를 `ZLinkSpotRefResolver`로 변경한다.
- `resolveSpotAddressAsync`를 `resolveSpotRefAsync`로 변경한다.
- `resolveActorSpotAddressAsync`를 `resolveActorSpotRefAsync`로 변경한다.
- actor 메시징 API는 `ActorRef`를 받는다.
- spot 메시징 API는 `SpotRef`를 받는다.
- `ZLinkSpotRemoteAddress` / `ZLinkSpotRemoteAddressResolver`는 일반 guide/sample에서 제거하고,
  유지가 필요하면 advanced routing extension으로 분리한다.

## 주요 파일

```text
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorClient.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorDirectory.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorJoinResult.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorManager.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorRef.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorRefSnapshot.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/channels/ZLinkRouteClient.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations/ZLinkActorAddressResolver.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations/ZLinkActorLocation.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations/ZLinkSpotAddress.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/locations/ZLinkSpotAddressResolver.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkActorClientRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/locations/ZLinkStoreLocationResolvers.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/spots/ZLinkSpotRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/ZLinkSpotOutbound.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/ZLinkSpotRemoteAddress.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/ZLinkSpotRemoteAddressResolver.java
framework/languages/java/e2e/SpotService/Shared/src/main/java/systems/zlink/e2e/spotservice/shared/SpotRouteResolver.java
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/RemoteSpotYieldSessionHandler.java
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/ScenarioReqHandler.java
framework/languages/java/e2e/YieldDispatch/Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/SpotCommandHandler.java
framework/languages/java/samples/java/Bingo/Server/Session/src/main/java/systems/zlink/samples/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CourierGateway/src/main/java/systems/zlink/samples/deliverydispatch/server/couriergateway/handlers/BindCourierHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CourierGateway/src/main/java/systems/zlink/samples/deliverydispatch/server/couriergateway/handlers/OfferDeliveryHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CustomerGateway/src/main/java/systems/zlink/samples/deliverydispatch/server/customergateway/handlers/EnsureCustomerActorHandler.java
framework/languages/java/samples/java/DeliveryDispatch/Server/CustomerGateway/src/main/java/systems/zlink/samples/deliverydispatch/server/customergateway/sessions/handlers/SubscribeDeliverySessionHandler.java
framework/languages/java/samples/java/ShoppingMall/Server/OrderWorkflow/src/main/java/systems/zlink/samples/shoppingmall/server/orderworkflow/OrderWorkflowService.java
```

## 테스트

추가/수정해야 할 테스트:

```text
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/LocationContractTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/actors/ZLinkActorClientRuntimeTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/channels/ZLinkChannelRuntimeTest.java
framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/locations/ZLinkStoreLocationResolversTest.java
framework/languages/java/zlink-framework-core/src/integrationTest/java/systems/zlink/framework/runtime/ChannelMessagingTest.java
framework/languages/java/zlink-framework-core/src/contractTest/java/systems/zlink/framework/locations/LocationStoreContractTest.java
```

필수 검증:

- `sendToActor(ActorRef, ...)` / `requestToActor(ActorRef, ...)`가 동작한다.
- `sendToSpot(SpotRef, ...)` / `requestToSpot(SpotRef, ...)`가 동작한다.
- id-only messaging API가 public contract에 없다.
- ref 기반 전송 중 location resolver/store가 호출되지 않는다.
- stale `SpotRef` 실패 분류가 기존 계약과 맞다.

## 문서 변경 대상

코드와 테스트를 바꾸는 같은 작업 안에서 Java 문서와 관련 공통 문서를 함께 수정한다. 문서 수정은
별도 worker로 넘기지 않는다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/common
framework/doc/framework/java
```

사용자-facing 문서에는 `ActorRef` / `SpotRef` 기반 전송만 남긴다. `JoinSpot(spotRid, ...)`처럼
lifecycle id 입력이 남아야 하는 경우에는 일반 메시징 API가 아니라는 설명을 붙인다.

## 메시지 핸들러 등록 정책 동시 적용

Ref 대상 통일 작업 중 Java sample이나 E2E의 handler 등록 표면을 고치면
`framework/doc/framework/common/spec/framework-api.ko.md`의 `Handler 등록 정책`도 같은 범위에서
적용한다. handler 타입과 metadata로 알 수 있는 packet 이름, actor 타입, request/send/subscription
종류는 등록 호출부에 반복 인자로 넘기지 않는다.

sample 정리는 아래 기준으로 함께 진행한다.

- `TicTacToe` Java sample은 manual handler registration을 보여 주는 예시로 남긴다.
- `Bingo`, `DeliveryDispatch`, `ShoppingMall`, `SupportChat`, `GameQuest` Java sample은 manual
  registration을 제거하고 automatic registration만 사용하도록 정리한다.
- 자동 등록과 수동 등록이 같은 dispatch key를 만들면 startup validation 오류로 처리한다. 조용히
  덮어쓰거나 특정 sample만 통과시키는 helper로 처리하지 않는다.
- README와 guide는 TicTacToe를 수동 등록 예시로, 나머지 sample을 자동 등록 예시로 설명한다.

## connection 복구 책임 경계

Java framework는 이미 core/binding에 넘긴 connection의 복구를 직접 구현하지 않는다. 연결된
connection의 끊김 감지, reconnect interval, reconnect backoff, monitor 기반 reconnect는 core 또는
binding socket option 책임이다. Java framework가 할 수 있는 일은 location/topology desired set을
계산하고, 아직 성공적으로 core에 맡기지 못한 target에 initial `connect`를 다음 tick에서 다시
요청하는 것까지다.

감사 대상:

```text
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/locations/ZLinkAutoConnectReconciler.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/locations/ZLinkLocationAutoConnectHost.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/locations/ZLinkAutoConnectLoop.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkActorClientRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkSessionActorsRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkNativeBoundSessionRuntime.java
framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkBoundSessionRuntime.java
```

처리 기준:

1. `ZLinkAutoConnectReconciler`의 `reconnect disconnect/connect` 로그와 코드는 established connection
   recovery로 오해되지 않게 정리한다. 실제 의미가 endpoint/owner 변경이면 `topology handover`로
   이름과 주석을 바꾼다.
2. connect 실패 시 target을 active/connected로 남기면 안 된다. initial connect 실패는 다음 topology
   tick에서 다시 시도해야 하며, 성공한 connection처럼 기록하지 않는다.
3. disconnected event를 trigger로 같은 endpoint reconnect loop를 돌리는 코드가 있으면 제거한다.
4. actor/session runtime의 `retry`, `route-ready`, `native-bound-session-retry` 계열 코드는 연결
   복구인지 binding 준비 수렴 대기인지 분류한다. binding 준비 수렴 대기라면 timeout과 목적을 주석과
   테스트명에 드러낸다. 연결 복구라면 제거하고 core/binding 버그로 분리한다.
5. Java E2E나 sample이 framework reconnect를 전제로 대기하거나 sleep으로 복구를 숨기면 수정 대상에
   포함한다.

완료 보고에는 `reconnect` 이름이 topology handover로 정리되었는지, actor/session retry가 연결 복구가
아니라는 근거 또는 분리한 버그, `.NET` 정책과 Java 정책이 달라지지 않았다는 비교를 포함한다.

## 완료 게이트

```bash
cd framework/languages/java
./gradlew :zlink-framework-core:test :zlink-framework-core:contractTest :zlink-framework-core:integrationTest
./e2e/SpotService/run_e2e.sh
./e2e/YieldDispatch/run_e2e.sh
./samples/java/Bingo/run_sample.sh
./samples/java/DeliveryDispatch/run_sample.sh
./samples/java/ShoppingMall/run_sample.sh

rg -n "ZLinkActorRef|ZLinkActorRefSnapshot|ZLinkSpotAddress|resolveSpotAddress|resolveActorSpotAddress|sendToActor\\([^)]*actorId|requestToActor\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  zlink-framework-core e2e samples/java \
  -S -g '!**/build/**'

rg -n "ZLinkActorRef|ZLinkActorRefSnapshot|SpotAddress|spot address|SpotRemoteAddress|spot remote address|sendToActor\\([^)]*actorId|requestToActor\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  ../../doc/contract-inventory ../../doc/framework/common ../../doc/framework/java \
  -S -g '!../../doc/plan/**' -g '!../../doc/**/draft/**'

rg -n "reconnect|retry|backoff|disconnect.*connect|connect.*disconnect|route-ready|native-bound-session-retry" \
  zlink-framework-core/src/main/java/systems/zlink/framework/runtime e2e samples/java \
  -S -g '!**/build/**'
```

Java E2E/sample gap 제거 계획의 완료 게이트도 함께 통과해야 한다.

```bash
cd framework/languages/java
./gradlew build
./gradlew test
./gradlew sampleTest
for f in e2e/*/run_e2e.sh; do timeout 420s "$f"; done
ZLINK_SAMPLE_FILTER= timeout 900s samples/run_samples.sh

rg -n '\\|[^\\n]*\\|[^\\n]*\\|[^\\n]*\\|[[:space:]]*(partial|gap|pending)[[:space:]]*\\|' \
  e2e/*/feature-map.ko.md \
  e2e/*/porting-inventory.ko.md \
  samples/java/*/sample-porting-inventory.ko.md
```

완료 전에는 별도 read-only 누락 리뷰를 요청한다. 리뷰 결과가 `NO MISSING JAVA ITEMS`가 아니면 finding을
수정하고 같은 범위를 다시 검증한다. 그 뒤 POSD/DDD 리뷰가 `NO POSD/DDD JAVA REFACTOR ITEMS`를 반환해야
이 worker를 완료로 본다.
