# Kotlin Worker Prompt: ActorRef / SpotRef 전송 대상 통일

기준 문서: `framework/doc/plan/framework-ref-target-unification-plan.ko.md`

## 목표

Kotlin extension, Kotlin sample, Kotlin e2e를 Java framework 변경에 맞춘다. Kotlin은 Java public
contract 위에 coroutine helper를 제공하므로, Java 쪽 `ActorRef` / `SpotRef` rename을 그대로 따른다.

## Naming 규칙

| 현재 이름 | 최종 이름 |
|-----------|-----------|
| `ZLinkActorRef` | `ActorRef` |
| `ZLinkActorRefSnapshot` | `ActorRefSnapshot` |
| `ZLinkSpotAddress` | `SpotRef` |

Kotlin 전용 helper도 값 개념에는 `ZLink`를 붙이지 않는다. Manager, client, store, runtime 같은
service/role 타입은 Java 이름을 따른다.

## 제거 대상

```text
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkFrameworkExtensions.kt
  sendToActorAwait(actorId, ...)
  requestToActorAwait(actorId, ...)
  sendToSpot(..., address, ...)
  requestToSpot(..., address, ...)
```

`sendToSpot` / `requestToSpot` helper는 `SpotRef` 인자로 교체한다. actor messaging helper는
`ActorRef` 인자로 교체한다.

## 추가/변경 대상

- `ActorRef` 인자 `sendToActorAwait` / `requestToActorAwait`를 제공한다.
- `SpotRef` 인자 `sendToSpot` / `requestToSpot` helper를 제공한다.
- `resolveSpotAddress`를 `resolveSpotRef`로 변경한다.
- `resolveActorSpotAddress`를 `resolveActorSpotRef`로 변경한다.
- Kotlin sample/e2e의 `ZLinkSpotAddress(...)` 생성자를 `SpotRef(...)`로 변경한다.
- Kotlin sample/e2e의 `ZLinkActorRef` import를 `ActorRef`로 변경한다.

## 주요 파일

```text
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkFrameworkExtensions.kt
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkLocationExtensions.kt
framework/languages/java/zlink-framework-kotlin/src/test/kotlin/systems/zlink/framework/kotlin/KotlinFrameworkExtensionsContractTest.kt
framework/languages/java/e2e-kotlin/SpotService/Server/MultiNode/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/multinode/MultiNodeApplication.kt
framework/languages/java/e2e-kotlin/SpotService/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/SpotRouteResolver.kt
framework/languages/java/e2e-kotlin/YieldDispatch/Server/Session/src/main/java/systems/zlink/e2e/kotlin/yielddispatch/RemoteSpotYieldReqRouteHandler.java
framework/languages/java/e2e-kotlin/YieldDispatch/Server/Session/src/main/java/systems/zlink/e2e/kotlin/yielddispatch/SpotMsgRouteHandler.java
framework/languages/java/samples/kotlin/Bingo/Server/Session/src/main/kotlin/systems/zlink/samples/kotlin/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CourierGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/couriergateway/handlers/BindCourierHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CourierGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/couriergateway/handlers/OfferDeliveryHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CustomerGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/customergateway/handlers/EnsureCustomerActorHandler.kt
```

## 테스트

추가/수정해야 할 테스트:

```text
framework/languages/java/zlink-framework-kotlin/src/test/kotlin/systems/zlink/framework/kotlin/KotlinFrameworkExtensionsContractTest.kt
framework/languages/java/e2e-kotlin/SpotService/run_e2e.sh
framework/languages/java/e2e-kotlin/YieldDispatch/run_e2e.sh
```

필수 검증:

- `sendToActorAwait(ActorRef, ...)` / `requestToActorAwait(ActorRef, ...)`가 동작한다.
- `sendToSpot(..., SpotRef, ...)` / `requestToSpot(..., SpotRef, ...)`가 동작한다.
- id-only messaging helper가 없다.
- Kotlin sample/e2e가 `ActorRef` / `SpotRef` 기반으로 컴파일된다.

## 문서 변경 대상

코드와 테스트를 바꾸는 같은 작업 안에서 Kotlin 문서와 관련 공통 문서를 함께 수정한다. 문서 수정은
별도 worker로 넘기지 않는다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/common
framework/doc/framework/kotlin
```

사용자-facing 문서에는 `ActorRef` / `SpotRef` 기반 전송만 남긴다. Kotlin 문서가 Java API를 설명하는
경우에도 old Java 이름을 남기지 않는다.

## 메시지 핸들러 등록 정책 동시 적용

Ref 대상 통일 작업 중 Kotlin sample이나 E2E의 handler 등록 표면을 고치면
`framework/doc/framework/common/spec/framework-api.ko.md`의 `Handler 등록 정책`도 같은 범위에서
적용한다. Kotlin extension이 Java core 등록 표면을 감싸더라도, handler 타입과 metadata로 알 수 있는
packet 이름, actor 타입, request/send/subscription 종류를 호출부에 반복해서 받는 helper를 새로 만들지
않는다.

sample 정리는 아래 기준으로 함께 진행한다.

- `TicTacToe` Kotlin sample은 manual handler registration을 보여 주는 예시로 남긴다.
- `Bingo`, `DeliveryDispatch`, `ShoppingMall`, `SupportChat`, `GameQuest` Kotlin sample은 manual
  registration을 제거하고 automatic registration만 사용하도록 정리한다.
- Kotlin automatic registration은 test-only scan helper가 아니라 실제 Spring/Kotlin framework 등록
  표면을 사용해야 한다.
- README와 guide는 TicTacToe를 수동 등록 예시로, 나머지 sample을 자동 등록 예시로 설명한다.

## connection 복구 책임 경계

Kotlin은 Java core 위의 coroutine/extension 계층이다. Kotlin extension, sample, E2E는 established
connection reconnect loop를 직접 구현하지 않는다. 연결된 connection의 끊김 감지와 복구는 core/binding
책임이고, Java core가 제공하는 auto-connect 또는 actor/session readiness 대기를 Kotlin에서 다시
감싸며 추가 retry 정책을 만들지 않는다.

감사 대상:

```text
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkConnectorExtensions.kt
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/
framework/languages/java/samples/kotlin/
framework/languages/java/e2e-kotlin/
```

처리 기준:

1. `ZLinkConnectorExtensions.kt`처럼 `inner.reconnect()`나 reconnect option을 그대로 감싸는 wrapper는
   유지할 수 있다. 이것은 재구현이 아니다.
2. coroutine loop, flow retry, timer, delay/backoff로 established connection reconnect를 직접 구현한
   코드가 있으면 제거한다.
3. Kotlin sample과 E2E가 Java core의 연결 복구 정책과 다른 의미의 reconnect를 설명하거나 기대하지
   않게 정리한다.
4. core/binding 기능 부족으로 보이는 문제가 드러나면 Kotlin만 우회하지 말고 Java 또는 binding 수준
   재현 여부를 확인한 뒤 별도 버그로 분리한다.

## 완료 게이트

```bash
cd framework/languages/java
./gradlew :zlink-framework-kotlin:test
./e2e-kotlin/SpotService/run_e2e.sh
./e2e-kotlin/YieldDispatch/run_e2e.sh

rg -n "ZLinkActorRef|ZLinkActorRefSnapshot|ZLinkSpotAddress|resolveSpotAddress|resolveActorSpotAddress|sendToActorAwait\\([^)]*actorId|requestToActorAwait\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  zlink-framework-kotlin e2e-kotlin samples/kotlin \
  -S -g '!**/build/**'

rg -n "ZLinkActorRef|ZLinkActorRefSnapshot|SpotAddress|spot address|SpotRemoteAddress|spot remote address|sendToActorAwait\\([^)]*actorId|requestToActorAwait\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  ../../doc/contract-inventory ../../doc/framework/common ../../doc/framework/kotlin \
  -S -g '!../../doc/plan/**' -g '!../../doc/**/draft/**'

rg -n "reconnect|retry|backoff|delay\\(|Timer|Scheduled|loop" \
  zlink-framework-kotlin samples/kotlin e2e-kotlin \
  -S -g '!**/build/**'
```

마지막 검색 결과는 wrapper, 테스트, 또는 Java core 준비 대기 호출이어야 한다. Kotlin 고유 reconnect
loop가 남으면 완료로 보지 않는다.
