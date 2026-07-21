# Kotlin Channel messaging 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Channel](../../java/interfaces/channel-messaging.ko.md)

Kotlin은 Java Channel client와 call을 사용하고 `CompletionStage.await()`로 기다린다. Coroutine extension은
ChannelName과 typed reply를 관용적으로 투영하지만 별도 request 상태 기계를 만들지 않는다.

## Kotlin source signature

```kotlin
interface ZLinkSuspendingRequestHandler<TRequest, TReply> {
    suspend fun handle(request: TRequest, context: ZLinkRequestContext): TReply
}

interface ZLinkSuspendingSendHandler<TMessage> {
    suspend fun handle(message: TMessage, context: ZLinkSendContext)
}

interface ZLinkSuspendingPublishHandler<TMessage> {
    suspend fun handle(message: TMessage, context: ZLinkPublishContext)
}

interface ZLinkSuspendingRouteRequestHandler<TRequest, TReply> {
    suspend fun handle(request: TRequest, context: ZLinkRouteRequestContext): TReply
}

interface ZLinkSuspendingRouteSendHandler<TMessage> {
    suspend fun handle(message: TMessage, context: ZLinkRouteSendContext)
}

suspend fun <TReply> ZLinkRequestCall.awaitReply(
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply
suspend fun <TReply> ZLinkRequestCall.yieldReply(
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkRequestCall.yieldReply(): TReply

fun <TMessage> ZLinkClient.send(
    channelName: String,
    message: TMessage,
): ZLinkSendCall
suspend inline fun <reified TReply> ZLinkClient.request(
    channelName: String,
    message: Message,
): TReply

fun <TEvent> ZLinkFanoutClient.publishToTopic(
    channelName: String,
    topic: String,
    message: TEvent,
): ZLinkFanoutPublishCall
fun <TEvent> ZLinkFanoutClient.publishToTopic(
    channelName: String,
    message: TEvent,
): ZLinkFanoutPublishCall

fun <TMessage> ZLinkRouteClient.send(
    meshName: String,
    target: RoutingId,
    message: TMessage,
): ZLinkSendCall
fun <TMessage> ZLinkRouteClient.send(
    channelName: String,
    message: TMessage,
): ZLinkSendCall
suspend inline fun <reified TReply> ZLinkRouteClient.request(
    meshName: String,
    target: RoutingId,
    message: Message,
): TReply
suspend inline fun <reified TReply> ZLinkRouteClient.request(
    channelName: String,
    message: Message,
): TReply

public fun messageOf(value: Any): ZLinkMessage
public inline fun <reified T> ZLinkMessage.decode(): T
```

Topic을 받는 `publishToTopic(...)`에 내부 liveness용 exact byte `01 5A 4C 46 31`을 전달하면 transport를
시작하지 않고 Java runtime의 `ZLinkConfigurationException`을 발생시킨다. Topic을 생략한 overload는 typed
event의 packet name을 사용하므로 이 내부 topic을 만들지 않는다.

RouteMesh DSL은 Java builder의 의미를 바꾸지 않고 receiver와 lambda만 제공한다. MeshNode 하나의 physical
connection 위에 ChannelName별 role을 구성한다.

```kotlin
fun ZLinkFrameworkOptions.routeMesh(
    meshName: String,
    configure: ZLinkMeshNodeBuilder.() -> Unit,
): ZLinkMeshNodeBuilder

fun ZLinkMeshNodeBuilder.channel(
    channelName: String,
    configure: ZLinkMeshChannelBuilder.() -> Unit = {},
): ZLinkMeshChannelBuilder

fun ZLinkMeshPeerConnections.connect(
    expectedRoutingId: RoutingId,
    endpoint: String,
)
```

```kotlin
val reply = routeClient
    .requestToChannel("inventory", request)
    .submit(InventoryReply::class.java)
    .await()
```

## Exact generated JVM signature

아래 JVM signature는 Kotlin source contract의 generated form이다.

```java
public final class systems.zlink.framework.kotlin.ZLinkMessageExtensionsKt {
  public static final systems.zlink.framework.messaging.ZLinkMessage messageOf(java.lang.Object);
  public static final <T> T decode(systems.zlink.framework.messaging.ZLinkMessage);
}
public final class systems.zlink.framework.kotlin.ZLinkRouteMeshExtensionsKt {
  public static final systems.zlink.framework.configuration.ZLinkMeshNodeBuilder routeMesh(systems.zlink.framework.configuration.ZLinkFrameworkOptions, java.lang.String, kotlin.jvm.functions.Function1<? super systems.zlink.framework.configuration.ZLinkMeshNodeBuilder, kotlin.Unit>);
  public static final systems.zlink.framework.configuration.ZLinkMeshChannelBuilder channel(systems.zlink.framework.configuration.ZLinkMeshNodeBuilder, java.lang.String, kotlin.jvm.functions.Function1<? super systems.zlink.framework.configuration.ZLinkMeshChannelBuilder, kotlin.Unit>);
  public static systems.zlink.framework.configuration.ZLinkMeshChannelBuilder channel$default(systems.zlink.framework.configuration.ZLinkMeshNodeBuilder, java.lang.String, kotlin.jvm.functions.Function1, int, java.lang.Object);
  public static final void connect(systems.zlink.framework.configuration.ZLinkMeshPeerConnections, systems.zlink.contracts.core.RoutingId, java.lang.String);
}
public final class systems.zlink.framework.kotlin.ZLinkSuspendingHandlersKt {
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingPublishHandler<TMessage> {
  public abstract java.lang.Object handle(TMessage, systems.zlink.framework.channels.ZLinkPublishContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler<TRequest, TReply> {
  public abstract java.lang.Object handle(TRequest, systems.zlink.framework.channels.ZLinkRequestContext, kotlin.coroutines.Continuation<? super TReply>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingRouteRequestHandler<TRequest, TReply> {
  public abstract java.lang.Object handle(TRequest, systems.zlink.framework.channels.ZLinkRouteRequestContext, kotlin.coroutines.Continuation<? super TReply>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingRouteSendHandler<TMessage> {
  public abstract java.lang.Object handle(TMessage, systems.zlink.framework.channels.ZLinkRouteSendContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSendHandler<TMessage> {
  public abstract java.lang.Object handle(TMessage, systems.zlink.framework.channels.ZLinkSendContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
```
