<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Java STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [Actor/session](./spring-boot-actor-session.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework Java STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` `STREAM` 초안을 코드 흐름으로 보기 위한 샘플 문서다.

## 1. Header session

```java
@Component
public final class RouteSession implements ZLinkSession {
    private final ZLinkSessionContext context;

    public RouteSession(ZLinkSessionContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onDispatchAsync(
        ZLinkStreamHeader header,
        Message payload) {
        return context.client()
            .reply(new Pong())
            .submit();
    }
}
```

## 2. Actor relay

```java
@Component
public final class ActorRelaySession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkActorManager actorManager;

    public ActorRelaySession(
        ZLinkSessionContext context,
        ZLinkActorManager actorManager) {
        this.context = context;
        this.actorManager = actorManager;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onDispatchAsync(
        ZLinkStreamHeader header,
        Message payload) {
        return actorManager.getOrCreate("player-42", "player")
            .thenCompose(actor -> context.actors().bind(actor))
            .thenCompose(bound -> bound.relay(header, payload));
    }
}
```

`STREAM`은 recv loop를 직접 드러내기보다 session registration으로 설명한다. raw
session public type은 현재 포팅 기준이 아니다.
