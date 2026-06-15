<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Java STREAM Open Items](../../draft/stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[Java 문서](../../README.ko.md)

[Java 묶음](../../README.ko.md) | [STREAM](../../spec/spring-boot-stream.ko.md) | [Actor/session](../../spec/spring-boot-actor-session.ko.md) | [인터페이스](../../spec/handler-interfaces.ko.md)

# ZLink Framework Java STREAM Samples

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
    public void onDispatch(
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
    public void onDispatch(
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

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: Java STREAM Open Items](../../draft/stream-open-items.ko.md)
<!-- framework-adapter-nav:bottom:end -->
