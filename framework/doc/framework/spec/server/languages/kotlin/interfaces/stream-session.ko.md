# Kotlin STREAM session 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java STREAM session](../../java/interfaces/stream-session.ko.md)

Kotlin suspending session과 handler adapter는 Java registration과 JVM runtime queue를 사용한다. Coroutine
continuation을 연결하는 wrapper만 Kotlin package가 소유하며 별도 STREAM runtime이나 JNI 경로를 만들지 않는다.
Session Actor relay는 Java의 두 overload를 그대로 사용한다. Dispatch context를 받는 overload는
explicit current STREAM request reply capability를 runtime에 이전하며 terminal completion은 Actor typed reply 또는
Framework admission failure 중 하나다. Handshake failure는 session 생성 전 monitoring에만 기록된다.

## Kotlin source signature

```kotlin
interface ZLinkSuspendingTypedSessionPacketHandler<
    TSessionContext : ZLinkSessionContext,
    TMessage : Any,
> {
    fun packetName(): String
    fun messageType(): Class<TMessage>
    suspend fun handle(
        context: TSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        message: TMessage,
    )
}

abstract class ZLinkSuspendingSession : ZLinkSession {
    abstract override fun context(): ZLinkSessionContext
    protected open suspend fun onConnectedSuspending()
    protected open suspend fun onDisconnectedSuspending()
    protected open suspend fun onErrorSuspending(error: ZLinkStreamError)
    protected open suspend fun onDispatchSuspending(
        dispatch: ZLinkSessionDispatchContext,
        payload: ZLinkMessage,
    )
}

suspend fun ZLinkSessionActors.bindOrGetActor(
    actor: ActorRef,
): ZLinkSessionActor
```

Stream Connector의 Kotlin call과 test assertion type은 server package 계약에 포함하지 않는다. 해당 표면은
[Stream Connector Java/Kotlin 계약](../../../../stream-connector/languages/java/03-stream-connector.ko.md)이
소유한다.

## Exact generated JVM signature

아래 JVM signature는 Kotlin source contract의 generated form이다.

```java
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingSession implements systems.zlink.framework.streams.ZLinkSession {
  public systems.zlink.framework.kotlin.ZLinkSuspendingSession();
  public abstract systems.zlink.framework.streams.ZLinkSessionContext context();
  public final java.util.concurrent.CompletionStage<java.lang.Void> onConnected();
  public final java.util.concurrent.CompletionStage<java.lang.Void> onDisconnected();
  public final java.util.concurrent.CompletionStage<java.lang.Void> onError(systems.zlink.framework.streams.ZLinkStreamError);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onDispatch(systems.zlink.framework.streams.ZLinkSessionDispatchContext, systems.zlink.framework.messaging.ZLinkMessage);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingTypedSessionPacketHandler<TSessionContext extends systems.zlink.framework.streams.ZLinkSessionContext, TMessage> {
  public abstract java.lang.String packetName();
  public abstract java.lang.Class<TMessage> messageType();
  public abstract java.lang.Object handle(TSessionContext, systems.zlink.framework.streams.ZLinkSessionDispatchContext, TMessage, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
```
