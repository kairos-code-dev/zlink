<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Feature Map](./04-feature-map.ko.md) | [다음: Spot](./06-spot.ko.md)
<!-- framework-adapter-nav:end -->

# Java Channel Messaging Guide

## 1. 언제 쓰나

서버 간 request/reply, one-way send, event fanout이 필요할 때 channel messaging을
쓴다. 호출자는 endpoint가 아니라 channel name만 안다.

## 2. Request/reply

```java
client.requestToChannel("profile", new GetProfileRequest(accountId))
    .timeout(Duration.ofMillis(200))
    .submit(GetProfileReply.class);
```

server는 handler를 등록한다.

```java
@Component
public final class GetProfileHandler
    implements ZLinkRequestHandler<GetProfileRequest, GetProfileReply> {
    @Override
    public GetProfileReply handle(
        GetProfileRequest request,
        ZLinkRequestContext context) {
        return new GetProfileReply(request.accountId());
    }
}
```

## 3. Fanout

```java
fanoutClient.publish("profile", "profile.changed", new ProfileChanged(accountId))
    .submit();
```

fanout은 reply를 기대하지 않는 event 전파다.

## 4. Route mesh

route mesh는 target node `RoutingId`를 application이 직접 알고 있을 때만 쓴다.
session actor relay는 route mesh를 흉내 내지 않고 ActorGateway를 사용한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Feature Map](./04-feature-map.ko.md) | [다음: Spot](./06-spot.ko.md)
<!-- framework-adapter-nav:bottom:end -->
