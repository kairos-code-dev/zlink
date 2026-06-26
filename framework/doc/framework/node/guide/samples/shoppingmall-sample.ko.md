# ShoppingMall Sample (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — ShoppingMall](../../../common/sample/event/shoppingmall.ko.md)다.
> 이 문서는 같은 시나리오를 Node/NestJS framework 표면으로 구체화한다. payload codec은 JSON이다.

## 1. 목적

event-sourced 주문 workflow와 projection을 보여 준다. 같은 `OrderId`의 event는 항상 같은
`OrderWorkflow` owner 흐름에서 append 되고, projection은 event replay 만으로 재생성된다.
idempotency·dedupe·보상 트랜잭션을 framework primitive 위에서 구현한다.

## 2. 서버 구성

`Registry`, `Server`, `Client`로 구성한다. 현재 TypeScript 구현은 하나의 서버 프로세스 안에서
commerce API 역할과 `OrderWorkflow` 역할을 module로 나누고, workflow role service와 channel
handler가 event-sourced aggregate(stream replay로 복원), optimistic version check, projection
folding을 소유하는 compact 구현이다. 공통 시나리오의 최종 목표는 `OrderWorkflowSpot` owner 구조지만,
현재 TypeScript 샘플은 아직 Spot factory와 다중 workflow instance를 등록하지 않는다.

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
않음). 현재 compact 구현에서는 하나의 workflow role service가 `OrderId`별 event stream과
projection을 관리한다. `OrderId` 기준 owner routing과 다중 workflow instance 검증은 full 구조로
승격할 때 추가해야 한다.

## 6. 호출 표면 (객체-메시징)

```ts
const started = await client
  .requestToChannel("commerce.api", new StartOrderReq(idempotencyKey, items))
  .submit<StartOrderRes>();
```

high-level 호출은 업무 객체를 직접 주고받고, codec(JSON)·packet name은 framework 내부가 처리한다.

## 7. Client self-check

중복 `StartOrderReq`가 같은 `OrderId`를 반환하고 event 중복 append가 없는지, 보상 event가
append 되는지, projection rebuild 결과가 같은지, `startedIdempotencyCount`가 기대값인지
server-side assertion으로 확인한다.

## 8. 완료 기준

- event-sourcing·idempotency·dedupe·보상·projection rebuild가 모두 동작한다.
- JSON codec을 쓰고 readiness를 sleep으로 숨기지 않는다.
- high-level 호출이 업무 객체 기반이고 codec helper가 노출되지 않는다.
