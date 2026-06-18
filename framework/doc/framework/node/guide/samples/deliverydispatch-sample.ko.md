# DeliveryDispatch Sample (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — DeliveryDispatch](../../../common/sample/deliverydispatch/README.ko.md)다.
> 이 문서는 같은 시나리오를 Node/NestJS framework 표면으로 구체화한다. payload codec은 JSON이다.

## 1. 목적

배송 배차, timeout 재배정, 상태 fanout, 고객 stream push를 보여 준다. channel handler,
fanout subscriber, Spot actor join을 한 흐름으로 묶는다.

## 2. 서버 구성

`Registry`, `DispatchApi`, `DispatchCenter`(+ background dispatch worker/work queue),
`Courier`(A=`timeout-reassign`, B=`accept`, mode로 선택), `Tracking`(channel server +
fanout publisher + Spot mesh + customer actor + `DeliveryTrackingSpot`/`CustomerEntrySpot`),
`Session`(stream + actor gateway + fanout subscriber + Spot mesh) 분리. Registry/Discovery로
자동 발견한다.

## 3. 전체 흐름

1. 고객이 배송 생성을 요청하면 `DispatchApi`가 받아 `DispatchCenter`에 배차를 맡긴다.
2. `DispatchCenter`가 courier에 배정한다. Courier A가 `delivery-reassign` 건을 timeout
   하면 Courier B로 재배정된다.
3. 상태 변화(`Assigned → Accepted → PickedUp → Delivered`)가 fanout으로 전파되고, 고객
   stream으로 push 된다. 재배정 건은 `Assigned → Reassigned → Accepted → PickedUp → Delivered`.

## 4. 비동기 진행 관용구

dispatch worker는 Promise 기반 비동기 loop로 배차를 진행하고, courier "무응답"과 client
polling은 관찰 가능한 상태/timer로 표현한다. readiness를 sleep으로 숨기지 않는다(registry
query, connector ready event 같은 관찰 가능한 신호를 기다린다).

## 5. 경계 메모

Node framework에는 ASP.NET 같은 HTTP 경계가 없으므로 dotnet의 HTTP `POST /deliveries`는
`deliverydispatch.api` client-server channel(`CreateDeliveryReq → DeliveryCreated`,
`ServerAssertionReq → ServerAssertionRes`)로 모델링한다. 역할·메시지 의미는 spec대로 유지한다.
cross-process Session↔Tracking Spot routing은 route-mesh channel과 registry 기반 Spot remote
주소 관용구를 쓴다.

## 6. 호출 표면 (객체-메시징)

```ts
const created = await client
  .requestToChannel("deliverydispatch.api", new CreateDeliveryReq(orderId))
  .submit<DeliveryCreated>();
```

fanout 구독은 framework subscriber 표면으로 받고, codec(JSON)·packet name은 framework가 처리한다.

## 7. Client self-check

`subscribe` 후 `delivery-success`/`delivery-reassign`의 상태 순서, 재배정 건이 `courier-b`
처리인지, server evidence(`ServerAssertionReq`)가 두 delivery의 상태 순서를 누락 없이 기록했는지
확인한다.

## 8. 완료 기준

- 역할 분리·fanout·재배정 timer·고객 push가 모두 동작한다.
- JSON codec을 쓰고 readiness를 sleep으로 숨기지 않는다.
- high-level 호출이 업무 객체 기반이고 codec helper가 노출되지 않는다.
