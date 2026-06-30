# Java Yield Cancellation API 초안

> 이 문서는 구현 전 초안이다. 현재 Java/Kotlin framework 공개 계약이 아니며,
> `framework/doc/framework/java/spec/`의 정식 spec 문서에 반영된 API가 아니다.

## 목적

Spot, Entry Spot, actor, timer handler가 `yield(...)`로 외부 작업을 기다리는 동안 취소 신호가 오면
대기 중인 작업과 continuation을 정리할 수 있게 한다. 이 계약이 있어야 공통 E2E Config 8의 `YD-E2`
cancellation cleanup을 내부 helper나 raw frame 우회 없이 검증할 수 있다.

## 현재 상태

현재 Java public surface는 아래 terminator만 제공한다.

```java
ZLinkRequestCall.yield(Class<TReply> replyType);
ZLinkActorJoinSpotCall.yield();
ZLinkActorJoinSpotCall.yield(Class<TReply> replyType);
ZLinkActorJoinEntrySpotCall.yield();
ZLinkActorJoinEntrySpotCall.yield(Class<TReply> replyType);
ZLinkWorkerCall<T>.yield();
```

handler는 `CancellationToken`을 받을 수 있지만, 그 token을 `yield(...)` 대기에 전달하는 public
terminator는 없다. 따라서 `.NET`의 `Yield<TReply>(token)`처럼 yield 대기 자체를 취소하는 시나리오는
현재 Java public API만으로 구현할 수 없다.

## 후보 API

기존 terminator 의미를 유지하면서 token을 명시적으로 받는 overload를 추가한다.

```java
public interface ZLinkRequestCall {
    <TReply> TReply yield(Class<TReply> replyType, CancellationToken cancellationToken);
}

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinResult<Void> yield(CancellationToken cancellationToken);
    <TReply> ZLinkActorJoinResult<TReply> yield(
        Class<TReply> replyType,
        CancellationToken cancellationToken);
}

public interface ZLinkActorJoinEntrySpotCall {
    ZLinkActorJoinResult<Void> yield(CancellationToken cancellationToken);
    <TReply> ZLinkActorJoinResult<TReply> yield(
        Class<TReply> replyType,
        CancellationToken cancellationToken);
}

public interface ZLinkWorkerCall<T> {
    T yield(CancellationToken cancellationToken);
}
```

기존 `yield(...)`는 `CancellationToken`이 없는 호출로 유지한다. 기존 호출자는 동작이 바뀌지 않는다.

## 동작 규칙

- `cancellationToken`은 `null`이면 안 된다.
- token이 이미 취소된 상태이면 request, actor join, worker 작업을 새로 시작하지 않고 취소 오류로 끝낸다.
- token이 yield 대기 중 취소되면 현재 handler continuation은 원래 mailbox에서 취소 오류를 관찰한다.
- 취소된 continuation은 reply, push, evidence marker 같은 사용자 후속 작업을 중복 수행하지 않는다.
- 취소 뒤 같은 Spot, actor, timer mailbox의 다음 작업은 정상 처리되어야 한다.
- timeout과 cancellation이 모두 발생할 수 있으면 먼저 관찰된 조건이 결과를 정한다.
- transport 또는 peer가 뒤늦게 reply를 보내도 취소된 public operation을 다시 완료시키지 않는다.
- framework shutdown 취소와 application handler token 취소는 같은 public cancellation path를 사용한다.

## 오류 표현

Java에는 현재 checked cancellation exception contract가 없다. 구현 전에는 아래 둘 중 하나를 선택해야
한다.

1. 기존 `CancellationToken`과 함께 쓰는 framework runtime exception을 추가한다.
2. `ZLinkFrameworkException`의 cancellation reason을 명확히 구분할 수 있게 한다.

선택한 오류는 `YD-E2` client와 handler가 public error 또는 정해진 cancellation result로 관찰할 수
있어야 한다.

## YD-E2 검증 조건

이 draft가 계약으로 채택되고 구현되면 Java YieldDispatch는 아래 흐름으로 `YD-E2`를 닫는다.

1. Play Spot handler가 delay service request를 cancellation-aware `yield(...)`로 기다린다.
2. handler가 server-side cancellation token을 지정한 시간 뒤 취소한다.
3. handler는 `cancel-yield-started`, `cancel-yield-released`, `cancel-yield-completed` marker를 남긴다.
4. 같은 Spot mailbox에 `ProbeCommand`를 보내 `probe-started`, `probe-completed` marker가 이어지는지 확인한다.
5. `cancel-yield-unexpected-resumed` marker가 없어야 한다.

## 구현 후 반영 위치

계약이 채택되고 구현과 regression test가 끝난 뒤에만 아래 정식 문서를 갱신한다.

- `framework/doc/framework/java/spec/handler-interfaces.ko.md`
- `framework/doc/framework/java/spec/spring-boot-spot.ko.md`
- `framework/doc/framework/java/spec/spring-boot-actor-session.ko.md`
- `framework/doc/framework/java/guide/05-spot.ko.md`
- `framework/doc/framework/java/internals/regression-test-matrix.ko.md`
