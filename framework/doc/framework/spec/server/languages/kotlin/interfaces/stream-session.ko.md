# Kotlin STREAM session 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java STREAM session](../../java/interfaces/stream-session.ko.md) ·
[session Actor dispatch](../../../31-session-actor-dispatch.ko.md)

Kotlin session lifecycle과 coroutine handler는 Java session 계약을 그대로 사용한다. Actor dispatch를 켜는
builder member는 `enableActorDispatch()`이며 MeshName 인자를 받지 않는다. Startup에는 object role이 Client
또는 Server인 Mesh와 Location Store가 필요하다. Global ActorId가 current authority와 Mesh를 결정한다.

Session bind는 exact `ActorRef`를 한 번 받는다. Local Actor instance나 ActorId만 받는 bind overload는 없다.
Bind 시 current mapping이 없으면 `ActorLocationStale`, generation이 다르면 `ActorGenerationStale`, pre-commit
seal 구간이면 `ActorMoving`이다. Framework는 hidden retry나 local fallback을 수행하지 않는다.

Session send·reply, bound session send와 Session Actor relay는 Kotlin one-way wrapper를 반환한다. Application은
`await(): Unit`으로 local STREAM queue admission만 기다리며 Java `CompletionStage`와 submission 결과 type을
직접 사용하지 않는다. Queue가 가득 차면 send timeout까지 기다리고 timeout, cancellation, route 단절과
runtime 종료는 exception으로 완료한다. Bound session이나 Session Actor mapping이 없으면
`ActorSessionNotBound`를 사용하고 runtime 종료에는 `RuntimeShutdown=36`을 사용한다.

## Kotlin source signature

```kotlin
suspend fun ZLinkSessionActors.bindOrGetActor(
    actor: ActorRef,
): ZLinkSessionActor

interface ZLinkKotlinSessionSendCall {
    fun metadata(key: String, value: String): ZLinkKotlinSessionSendCall
    fun compress(): ZLinkKotlinSessionSendCall
    suspend fun await()
}

interface ZLinkKotlinSessionReplyCall {
    fun compress(): ZLinkKotlinSessionReplyCall
    suspend fun await()
}

interface ZLinkKotlinSessionClient {
    fun send(message: Any): ZLinkKotlinSessionSendCall
    fun reply(message: Any): ZLinkKotlinSessionReplyCall
}

interface ZLinkKotlinSessionActor {
    fun relay(message: ZLinkMessage): ZLinkKotlinMessageSendCall
    fun relay(
        dispatch: ZLinkSessionDispatchContext,
        message: ZLinkMessage,
    ): ZLinkKotlinMessageSendCall
}

interface ZLinkKotlinBoundSession {
    fun send(message: Any): ZLinkKotlinMessageSendCall
}
```

## Exact generated JVM signature

```java
public final class systems.zlink.framework.kotlin.ZLinkFrameworkExtensionsKt {
  public static final java.lang.Object bindOrGetActor(systems.zlink.framework.streams.ZLinkSessionActors, systems.zlink.framework.actors.ActorRef, kotlin.coroutines.Continuation<? super systems.zlink.framework.streams.ZLinkSessionActor>);
}
public interface systems.zlink.framework.kotlin.ZLinkKotlinSessionSendCall {
  public abstract systems.zlink.framework.kotlin.ZLinkKotlinSessionSendCall metadata(java.lang.String, java.lang.String);
  public abstract systems.zlink.framework.kotlin.ZLinkKotlinSessionSendCall compress();
  public abstract java.lang.Object await(kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkKotlinSessionReplyCall {
  public abstract systems.zlink.framework.kotlin.ZLinkKotlinSessionReplyCall compress();
  public abstract java.lang.Object await(kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkKotlinSessionClient {
  public abstract systems.zlink.framework.kotlin.ZLinkKotlinSessionSendCall send(java.lang.Object);
  public abstract systems.zlink.framework.kotlin.ZLinkKotlinSessionReplyCall reply(java.lang.Object);
}
public interface systems.zlink.framework.kotlin.ZLinkKotlinSessionActor {
  public abstract systems.zlink.framework.kotlin.ZLinkKotlinMessageSendCall relay(systems.zlink.framework.messaging.ZLinkMessage);
  public abstract systems.zlink.framework.kotlin.ZLinkKotlinMessageSendCall relay(systems.zlink.framework.streams.ZLinkSessionDispatchContext, systems.zlink.framework.messaging.ZLinkMessage);
}
public interface systems.zlink.framework.kotlin.ZLinkKotlinBoundSession {
  public abstract systems.zlink.framework.kotlin.ZLinkKotlinMessageSendCall send(java.lang.Object);
}
```
