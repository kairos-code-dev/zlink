<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: SupportChat Sample](supportchat-sample.ko.md) | [다음: ShoppingMall Sample](shoppingmall-sample.ko.md)
<!-- framework-adapter-nav:end -->

[Java 묶음](../../README.ko.md) | [SPOT](../../spec/spring-boot-spot.ko.md) | [Actor/Session](../../spec/spring-boot-actor-session.ko.md) | [STREAM](../../spec/spring-boot-stream.ko.md)

# DeliveryDispatch Sample (Java/Kotlin)

> 언어 중립 시나리오 정본은 [공통 샘플 — DeliveryDispatch](../../../common/sample/deliverydispatch/README.ko.md)다.
> 실행 코드는 `samples/java/DeliveryDispatch`(Java), `samples/kotlin/DeliveryDispatch`(Kotlin)에 있다.

## 1. 목적

배송 배차, timeout 재배정, 상태 fanout, 고객 stream push를 보여 준다. channel handler,
fanout subscriber, Spot actor join을 한 흐름으로 묶는다. payload codec은 JSON이다.

## 2. 서버 구성

Java 샘플은 `.NET` 기준 구현과 같은 의미의 process 경계를 둔다. `Registry`가 discovery
endpoint를 제공하고, `Dispatch`는 JDK HTTP server로 `POST /deliveries`와 self-check endpoint를
연다. `Tracking`은 배송 상태 event를 기록하고 customer gateway로 알림을 보낸다.

고객 연결은 `CustomerGateway`가 맡는다. 이 role은 customer stream session, customer actor,
customer entry spot을 함께 가진다. 배송원 연결은 `CourierSession`이 받고, 배송원 actor의 위치
결정과 offer routing은 `CourierGateway`가 처리한다. 실제 courier actor는 `CourierSpotNode` 두
process에 나뉘어 생성된다.

## 3. 전체 흐름

1. client가 customer stream에 subscribe하고 courier-a, courier-b stream을 각각 bind한다.
2. client가 HTTP `POST /deliveries`로 배송을 만들면 `Dispatch` worker가 courier-a에 offer를
   보낸다.
3. 성공 흐름은 courier-a가 offer를 수락하고 `Assigned → Accepted → PickedUp → Delivered`
   상태가 customer stream으로 push 되는지 확인한다.
4. 재배정 흐름은 courier-a가 응답하지 않는 상황을 만들고, timeout 뒤 courier-b가 offer를 수락해
   `Assigned → Reassigned → Accepted → Delivered` 상태가 도착하는지 확인한다.

## 4. 경계 메모

외부 client 경계는 HTTP와 stream connector를 함께 사용한다. `Dispatch`는 JDK HTTP server로
배송 생성 요청과 server evidence self-check 요청을 받는다. 내부 role 사이의 배차, actor 보장,
상태 기록은 framework channel과 spot actor를 사용한다.

Courier bind 흐름은 public stream/session actor API만 사용한다. `CourierSession`이 courier stream
요청을 받고 `CourierGateway`에서 actor 위치를 확인한 뒤, 같은 stream dispatch 안에서 courier
actor로 bind packet을 relay한다. 이후 courier actor는 bound session으로 offer를 push한다.

## 6. Client self-check

`subscribe` 후 `delivery-success`/`delivery-reassign`의 상태 순서, 재배정 건이 `courier-b`
처리인지, server evidence endpoint가 두 delivery의 상태 순서를 누락 없이 기록했는지 확인한다.
성공 로그에 `deliverydispatch-reassignment=completed`,
`deliverydispatch-server-evidence=completed`, `deliverydispatch=completed`를 포함한다.

## 7. 완료 기준

- 역할 분리, 재배정 timeout, 고객 push가 모두 동작한다.
- JSON codec을 쓰고 framework runtime package나 private bridge에 의존하지 않는다.
- Java/Kotlin 두 샘플이 같은 역할, 메시지, 검증 순서를 따른다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: SupportChat Sample](supportchat-sample.ko.md) | [다음: ShoppingMall Sample](shoppingmall-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
