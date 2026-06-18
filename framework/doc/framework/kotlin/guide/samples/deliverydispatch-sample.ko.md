<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: SupportChat Sample](supportchat-sample.ko.md) | [다음: ShoppingMall Sample](shoppingmall-sample.ko.md)
<!-- framework-adapter-nav:end -->

[Kotlin 묶음](../../README.ko.md) | [SPOT](../../../java/spec/spring-boot-spot.ko.md) | [Actor/Session](../../../java/spec/spring-boot-actor-session.ko.md) | [STREAM](../../../java/spec/spring-boot-stream.ko.md)

# DeliveryDispatch Sample (Kotlin)

> 언어 중립 시나리오 정본은 [공통 샘플 — DeliveryDispatch](../../../common/sample/deliverydispatch/README.ko.md)다.
> 실행 코드는 `samples/java/DeliveryDispatch`(Java), `samples/kotlin/DeliveryDispatch`(Kotlin)에 있다.

## 1. 목적

배송 배차, timeout 재배정, 상태 fanout, 고객 stream push를 보여 준다. channel handler,
fanout subscriber, Spot actor join을 한 흐름으로 묶는다. payload codec은 JSON이다.

## 2. 서버 구성

`Registry`, `DispatchApi`, `DispatchCenter`(+ background `DispatchWorker`/`WorkQueue`),
`Courier`(A=`timeout-reassign`, B=`accept`, `--mode`로 선택), `Tracking`(channel server +
fanout publisher + Spot mesh + customer actor + `DeliveryTrackingSpot`/`CustomerEntrySpot`),
`Session`(stream + actor gateway + fanout subscriber + Spot mesh) 분리. Registry/Discovery
자동 발견.

## 3. 전체 흐름

1. 고객이 배송 생성을 요청하면 `DispatchApi`가 받아 `DispatchCenter`에 배차를 맡긴다.
2. `DispatchCenter`가 courier에 배정한다. Courier A가 `delivery-reassign` 건을 timeout
   하면 Courier B로 재배정된다.
3. 상태 변화(`Assigned → Accepted → PickedUp → Delivered`)가 fanout으로 전파되고, 고객
   stream으로 push 된다. 재배정 건은 `Assigned → Reassigned → Accepted → PickedUp → Delivered`.

## 4. 비동기 진행 관용구

`DispatchWorker`는 guarded `BlockingQueue.take()` 데몬 loop로 배차를 진행한다(`while(true)`/
`CountDownLatch` 미사용). courier "무응답"은 `LockSupport.parkNanos`로 표현한다(`Thread.sleep`
미사용 — 샘플 게이트 금지 패턴 준수).

## 5. 경계 메모

Java framework에는 ASP.NET 같은 HTTP 경계가 없으므로 dotnet의 HTTP `POST /deliveries`·
self-check은 `deliverydispatch.api` client-server channel(`CreateDeliveryRequest → DeliveryCreated`,
`ServerAssertionReq → ServerAssertionRes`)로 모델링한다. 역할·메시지 의미는 spec대로 유지한다.
cross-process Session↔Tracking spot routing은 route-mesh channel + `useRegistrySpotRemoteAddresses` +
`acceptSpotRoutesFromChannel` 관용구를 쓴다.

## 6. Client self-check

`subscribe` 후 `delivery-success`/`delivery-reassign`의 상태 순서, 재배정 건이 `courier-b`
처리인지, server evidence(`ServerAssertionReq`)가 두 delivery의 상태 순서를 누락 없이 기록했는지
확인한다. 성공 로그에 `deliverydispatch=completed` 등을 포함한다.

## 7. 완료 기준

- 역할 분리·fanout·재배정 timer·고객 push가 모두 동작한다.
- JSON codec을 쓰고 금지 패턴(Thread.sleep/while(true)/CountDownLatch)을 쓰지 않는다.
- Java/Kotlin 두 샘플이 같은 역할·메시지·검증 순서를 따른다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: SupportChat Sample](supportchat-sample.ko.md) | [다음: ShoppingMall Sample](shoppingmall-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
