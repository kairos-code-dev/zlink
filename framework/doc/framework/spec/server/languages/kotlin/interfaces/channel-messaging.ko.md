# Kotlin Channel messaging 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Channel](../../java/interfaces/channel-messaging.ko.md)

Kotlin은 Java Channel client와 call을 사용하고 `CompletionStage.await()`로 기다린다. Coroutine extension은
ChannelName과 typed reply를 관용적으로 투영하지만 별도 request 상태 기계를 만들지 않는다.

Spot direct send/request는 Channel call로 축소하지 않는다. Java `ZLinkRouteClient`가 반환하는
`ZLinkSpotSendCall`과 `ZLinkSpotRequestCall`을 유지해야 Missing Instance cold activation의
`instanceSpot`과 `inMesh`를 terminal submit 전에 구성할 수 있다. Kotlin의
Spot 전용 extension과 exact JVM signature는 [Spot 인터페이스](spots.ko.md)가 소유한다.

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

`yieldReply`는 Java `yield(...)`의 coroutine bridge일 뿐 임의 suspension을 Yield로 바꾸지 않는다.
`SPOT_WIDE` User Spot 또는 Instance Spot application handler가 아니면 coroutine을 suspend하거나 underlying
operation을 제출하기 전에 `InvalidConfiguration`으로 완료한다. Node direct request, Entry·`PER_ACTOR`,
Channel handler와 owner context 밖에도 같은 규칙을 적용한다. 현재 Spot gate가 필요한 target을 기다리는
일반 `awaitReply`도 submission 전에 거부한다. One-way extension은 FIFO queue admission을 유지하고 handler를
inline 또는 reentrant하게 호출하지 않는다.

Topic을 받는 `publishToTopic(...)`에 내부 liveness용 exact byte `01 5A 4C 46 31`을 전달하면 transport를
시작하지 않고 Java runtime의 `ZLinkConfigurationException`을 발생시킨다. Topic을 생략한 overload는 typed
event의 packet name을 사용하므로 이 내부 topic을 만들지 않는다.

RouteMesh DSL은 Java builder의 의미를 바꾸지 않고 receiver와 lambda만 제공한다. MeshNode 하나의 physical
connection 위에 ChannelName별 role을 구성한다.

RouteMesh Channel Server와 ClientServer Server weight는 Java builder의 signed `int`를 사용한다. 허용 범위는
`0..10000`, 기본값은 `100`이며 0은 새 target 선택에서 제외한다. Logical Multicast는 positive member를
각각 한 번만 포함하고 weight 크기로 제출 횟수를 늘리지 않는다. 범위 밖 startup·runtime 설정은
configuration error다.
Remote admitted count는 source의 local outbound transport queue 제출만 집계한다. Local admitted count는
origin node의 local Spot application queue 제출만 집계한다. Remote Spot queue 제출과 remote·local handler
실행 또는 완료는 coroutine bridge 완료 조건이 아니다.

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
