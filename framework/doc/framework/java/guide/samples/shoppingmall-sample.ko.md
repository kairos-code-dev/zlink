<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: DeliveryDispatch Sample](deliverydispatch-sample.ko.md) | [다음: GameQuest Sample](gamequest-sample.ko.md)
<!-- framework-adapter-nav:end -->

[Java 묶음](../../README.ko.md) | [SPOT](../../../common/spec/languages/java/spring-boot-spot.ko.md) | [Actor/Session](../../../common/spec/languages/java/spring-boot-actor-session.ko.md) | [STREAM](../../../common/spec/languages/java/spring-boot-stream.ko.md)

# ShoppingMall Sample (Java/Kotlin)

> 언어 중립 시나리오 정본은 [공통 샘플 — ShoppingMall](../../../common/sample/event/shoppingmall.ko.md)다.
> 실행 코드는 `samples/java/ShoppingMall`(Java), `samples/kotlin/ShoppingMall`(Kotlin)에 있다.

## 1. 목적

event-sourced 주문 workflow와 projection을 보여 준다. 같은 `OrderId`의 event는 항상 같은
`OrderWorkflow` instance 흐름에서 append 되고, projection은 event replay 만으로 재생성된다.
idempotency·dedupe·보상 트랜잭션을 framework primitive 위에서 구현한다. payload codec은 JSON이다.

## 2. 서버 구성

`Registry`, `CommerceApi`(x2: `api-a`/`api-b`), `OrderWorkflow`(x2: `workflow-a`/`workflow-b`),
`Client`. 현재 Java/Kotlin 구현은 `OrderWorkflow` role service와 channel handler가
event-sourced aggregate(stream replay로 복원), optimistic version check, projection folding을
소유하는 compact 구현이다. 공통 시나리오의 최종 목표는 `OrderWorkflowSpot` owner 구조지만,
현재 Java/Kotlin 샘플은 아직 Spot factory를 등록하지 않는다. scale-out(API x2 / workflow x2) 구성으로
서로 다른 주문이 서로 다른 instance에서 처리되고 어느 instance에서 조회해도 같은 projection을
반환함을 검증한다.

## 3. 전체 흐름

1. client가 `StartOrderReq`로 주문을 시작한다. 같은 `IdempotencyKey`/`SourceCommandId`는
   같은 `OrderId`로 모이고 event를 중복 append 하지 않는다.
2. workflow가 success / inventory-fail / payment-fail+compensation 세 saga 분기를 진행한다.
   결제 실패 후 `InventoryReleasedEvent` 보상이 append 된다.
3. projection을 삭제해도 `RebuildOrderProjectionReq`로 event replay 만으로 재생성된다.

## 4. 비동기 진행 관용구

`WorkflowSagaWorker`(guarded `BlockingQueue.take()` + 데몬 thread)가 saga를 진행하고, client는
`LockSupport.parkNanos`로 polling 한다. 금지 패턴(Thread.sleep/while(true)/CountDownLatch) 미사용.

## 5. 경계 메모

dotnet 행위 정본은 `samples/dotnet/.../ShoppingMall`이다(ShoppingMall dir에는 stale
artifact만 있었음). transport는 HTTP 대신 JSON-codec ZLink client/server channel을 쓴다(게이트가
HTTP를 요구하지 않음). workflow instance 선택은 `OrderId` 기준으로 안정적이다. 현재 샘플은
이 선택을 명시 endpoint routing으로 구현하며, Spot owner routing으로 구현하지는 않는다.

## 6. Client self-check

중복 `StartOrderReq`가 같은 `OrderId`를 반환하고 event 중복 append가 없는지, 보상 event가
append 되는지, 두 instance 어디서 조회해도 같은 projection인지, `startedIdempotencyCount`가
기대값인지 server-side assertion으로 확인한다.

## 7. 완료 기준

- event-sourcing·idempotency·dedupe·보상·projection rebuild·scale-out이 모두 동작한다.
- JSON codec을 쓰고 금지 패턴을 쓰지 않는다.
- Java/Kotlin 두 샘플이 같은 역할·메시지·검증 순서를 따른다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: DeliveryDispatch Sample](deliverydispatch-sample.ko.md) | [다음: GameQuest Sample](gamequest-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
