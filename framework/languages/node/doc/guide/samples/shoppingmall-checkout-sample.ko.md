# ShoppingMallCheckout Sample (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — ShoppingMallCheckout](../../../../../doc/spec/sample/event/shoppingmall-checkout.ko.md)다.
> 이 문서는 같은 시나리오를 Node/NestJS framework 표면으로 구체화한다. payload codec은 JSON이다.

## 1. 목적

event-sourced 주문 workflow와 projection을 보여 준다. 같은 `OrderId`의 event는 항상 같은
`OrderWorkflow` owner 흐름에서 append 되고, projection은 event replay 만으로 재생성된다.
idempotency·dedupe·보상 트랜잭션을 framework primitive 위에서 구현한다.

## 2. 서버 구성

`Registry`, `CommerceApi`(x2: `api-a`/`api-b`), `OrderWorkflow`(x2: `workflow-a`/`workflow-b`),
`Client`. `OrderWorkflow`가 event-sourced aggregate(stream replay로 복원), optimistic version
check, projection folding을 가진 상태 소유 Spot이다. scale-out(API x2 / workflow x2) 구성으로
서로 다른 주문이 서로 다른 instance에서 처리되고 어느 instance에서 조회해도 같은 projection을
반환함을 검증한다.

## 3. 전체 흐름

1. client가 `StartOrderReq`로 주문을 시작한다. 같은 `IdempotencyKey`/`SourceCommandId`는
   같은 `OrderId`로 모이고 event를 중복 append 하지 않는다.
2. workflow가 success / inventory-fail / payment-fail+compensation 세 saga 분기를 진행한다.
   결제 실패 후 `InventoryReleasedEvent` 보상이 append 된다.
3. projection을 삭제해도 `RebuildOrderProjectionReq`로 event replay 만으로 재생성된다.

## 4. 비동기 진행 관용구

saga worker는 Promise 기반 비동기 loop로 saga를 진행하고, client는 관찰 가능한 상태를
polling 한다. readiness를 sleep으로 숨기지 않는다.

## 5. 경계 메모

transport는 HTTP 대신 JSON-codec ZLink client/server channel을 쓴다(게이트가 HTTP를 요구하지
않음). owner routing은 `OrderId` 기준으로 안정적이고, 같은 `OrderId`는 항상 같은 workflow
instance owner 흐름으로 모인다.

## 6. 호출 표면 (객체-메시징)

```ts
const started = await client
  .requestToChannel("commerce.api", new StartOrderReq(idempotencyKey, items))
  .submit<StartOrderRes>();
```

high-level 호출은 업무 객체를 직접 주고받고, codec(JSON)·packet name은 framework 내부가 처리한다.

## 7. Client self-check

중복 `StartOrderReq`가 같은 `OrderId`를 반환하고 event 중복 append가 없는지, 보상 event가
append 되는지, 두 instance 어디서 조회해도 같은 projection인지, `startedIdempotencyCount`가
기대값인지 server-side assertion으로 확인한다.

## 8. 완료 기준

- event-sourcing·idempotency·dedupe·보상·projection rebuild·scale-out이 모두 동작한다.
- JSON codec을 쓰고 readiness를 sleep으로 숨기지 않는다.
- high-level 호출이 업무 객체 기반이고 codec helper가 노출되지 않는다.
