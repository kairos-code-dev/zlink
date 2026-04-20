[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework Java SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` `SPOT` 초안을 코드 흐름으로 보기 위한 샘플 문서다.

## 1. Spot request handler

```java
@Component
public final class StageHandlers {
    @ZLinkSpotRequestMapping
    public CompletionStage<GetStageStateReply> getStageStateAsync(
        GetStageStateRequest request,
        ZLinkSpotRequestContext context) {
        return CompletableFuture.completedFuture(
            new GetStageStateReply(context.self().spotRid(), 10)
        );
    }
}
```

## 2. Subscription handler

```java
@Component
public final class StageSubscriptions {
    @ZLinkSpotSubscription(topic = "stage.state.updated")
    public CompletionStage<Void> onStageStateAsync(
        StageStateUpdated event,
        ZLinkSpotSubscriptionContext context) {
        return CompletableFuture.completedFuture(null);
    }
}
```

## 3. Channel request from spot

```java
var reply = spotClient.requestChannelAsync(
    "profile",
    new GetProfileRequest(accountId),
    null
);
```

`SPOT` 안에서 다른 channel을 호출할 때도 기본은 `channel name` 기준이다.
