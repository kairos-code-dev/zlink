<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md)
<!-- framework-adapter-nav:end -->

[Kotlin 묶음](../../README.ko.md) | [STREAM](../../../java/spec/spring-boot-stream.ko.md) | [Actor/session](../../../java/spec/spring-boot-actor-session.ko.md) | [인터페이스](../../../java/spec/handler-interfaces.ko.md)

# ZLink Framework Kotlin STREAM Samples

## 1. Header session

```kotlin
@Component
class RouteSession(private val context: ZLinkSessionContext) : ZLinkSuspendingSession() {
    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
        context.client().reply(Pong()).submit().await()
    }
}
```

## 2. Actor relay

```kotlin
@Component
class ActorRelaySession(
    private val context: ZLinkSessionContext,
    private val actorManager: ZLinkActorManager,
) : ZLinkSuspendingSession() {
    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
        val actor = actorManager.getOrCreate("player-42", "player").await()
        val bound = context.actors().bind(actor).await()
        bound.relay(payload).await()
    }
}
```

`STREAM`은 recv loop를 직접 드러내기보다 session registration으로 설명한다. raw
session public type은 현재 포팅 기준이 아니다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md)
<!-- framework-adapter-nav:bottom:end -->
