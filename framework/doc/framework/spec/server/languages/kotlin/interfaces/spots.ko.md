# Kotlin Spot 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Spot](../../java/interfaces/spots.ko.md) ·
[Spot 공통 계약](../../../23-spot-actor.ko.md)

SpotRid는 Location Store transaction domain 전체에서 유일한 logical ID다. 일반 Spot send/request는
SpotRid만 받는다. `SpotRef(spotRid, objectGeneration, meshName, nodeRid)`는 exact incarnation을 close할 때만
사용하는 immutable snapshot이다. `objectGeneration`은 `1..Long.MAX_VALUE`이고 JSON에서는 decimal string이다.
User와 Instance Spot type은 UTF-8 1..255 bytes의 stable exact value다.
Java enum의 numeric value는 `ZLinkSpotKind.INVALID=0`, `ENTRY=1`, `USER=2`, `INSTANCE=3`이고
`ZLinkCreatableSpotKind.USER=1`, `INSTANCE=2`다. Kotlin은 ordinal을 계약 값으로 사용하지 않고 `value()`를
사용한다.

`ZLinkSpotManager.create(spotKind, spotType)`은 RID를 생성하고, `getOrCreate(spotRid, spotKind, spotType)`은
caller가 정한 RID를 사용한다. 둘 다 Java single-use fluent call을 반환하며 `inMesh`, `request`,
`placementProfile`, `affinityKey`, `timeout` 뒤 terminal `submit`을 정확히 한 번 호출한다. 중복 option과
중복 submit, Mesh 선택, type 충돌과 deadline 규칙은 Actor operation과 같다. `Create`에서 Ready object가
있으면 `SpotCreateFailed`, 다른 kind나 type이면 `SpotTypeMismatch`다. Entry Spot RID는 Framework가 만들며
public create 대상이 아니다.

## Kotlin source signature

```kotlin
interface ZLinkSuspendingSpotPacketHandler<TSpot : ZLinkSpot<*>, TMessage> {
    suspend fun handle(spot: TSpot, message: TMessage)
    suspend fun handle(
        spot: TSpot,
        message: TMessage,
        context: ZLinkSendContext,
    )
}

interface ZLinkSuspendingSpotRequestHandler<TSpot : Any, TRequest, TReply> {
    suspend fun handle(spot: TSpot, request: TRequest): TReply
    suspend fun handle(
        spot: TSpot,
        request: TRequest,
        context: ZLinkRequestContext,
    ): TReply
}

interface ZLinkSuspendingSpotSubscriptionHandler<TSpot : Any, TEvent> {
    suspend fun handle(spot: TSpot, event: TEvent)
    suspend fun handle(
        spot: TSpot,
        event: TEvent,
        context: ZLinkPublishContext,
    )
}

interface ZLinkSuspendingSpotTimerHandler<TSpot : ZLinkSpot<*>> {
    suspend fun handle(spot: TSpot, tick: ZLinkTimerTick)
}

abstract class ZLinkSuspendingSpot<TActor : ZLinkActor> : ZLinkSpot<TActor> {
    abstract override fun context(): ZLinkSpotContext
    protected open suspend fun onCreateSuspending(
        request: ZLinkMessage,
    ): ZLinkSpotCreateResponse
    protected open suspend fun onInitializeSuspending()
    protected open suspend fun onClosingSuspending()
    protected abstract suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse
    protected abstract suspend fun onJoinedActorSuspending(actor: TActor)
    protected abstract suspend fun onLeaveActorSuspending(actor: TActor)
    protected open suspend fun onDisconnectActorSuspending(actor: TActor)
}

abstract class ZLinkSuspendingEntrySpot<TActor : ZLinkActor> :
    ZLinkEntrySpot<TActor> {
    abstract override fun context(): ZLinkEntrySpotContext
    protected open suspend fun onInitializeSuspending()
    protected open suspend fun onClosingSuspending()
    protected abstract suspend fun onCreateActorSuspending(
        actor: TActor,
        createRequest: ZLinkMessage,
    )
    protected abstract suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse
    protected abstract suspend fun onJoinedActorSuspending(actor: TActor)
    protected abstract suspend fun onLeaveActorSuspending(actor: TActor)
    protected open suspend fun onDisconnectActorSuspending(actor: TActor)
}

inline fun <reified THandler : Any> ZLinkSpotHandlerRegistry.addHandler()

fun <TMessage> ZLinkRouteClient.send(
    spotRid: RoutingId,
    message: TMessage,
): ZLinkSendCall
suspend inline fun <reified TReply> ZLinkRouteClient.request(
    spotRid: RoutingId,
    message: Message,
): TReply
```

## Exact generated JVM signature

```java
public final class systems.zlink.framework.kotlin.ZLinkSpotHandlerRegistryExtensionsKt {
  public static final <THandler> void addHandler(systems.zlink.framework.spots.ZLinkSpotHandlerRegistry);
  public static final void addTypedHandler(systems.zlink.framework.spots.ZLinkSpotHandlerRegistry, java.lang.Class<?>);
}
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpot<TActor extends systems.zlink.framework.actors.ZLinkActor> implements systems.zlink.framework.spots.ZLinkEntrySpot<TActor> {
  public systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpot();
  public abstract systems.zlink.framework.spots.ZLinkEntrySpotContext context();
  public final java.util.concurrent.CompletionStage<java.lang.Void> onInitialize();
  public final java.util.concurrent.CompletionStage<java.lang.Void> onClosing();
  public final java.util.concurrent.CompletionStage<java.lang.Void> onCreateActor(TActor, systems.zlink.framework.messaging.ZLinkMessage);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.spots.ZLinkSpotActorJoinResponse> onActorJoin(java.lang.String, systems.zlink.framework.messaging.ZLinkMessage);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onJoinedActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onLeaveActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onDisconnectActor(TActor);
}
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingSpot<TActor extends systems.zlink.framework.actors.ZLinkActor> implements systems.zlink.framework.spots.ZLinkSpot<TActor> {
  public systems.zlink.framework.kotlin.ZLinkSuspendingSpot();
  public abstract systems.zlink.framework.spots.ZLinkSpotContext context();
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.spots.ZLinkSpotCreateResponse> onCreate(systems.zlink.framework.messaging.ZLinkMessage);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onInitialize();
  public final java.util.concurrent.CompletionStage<java.lang.Void> onClosing();
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.spots.ZLinkSpotActorJoinResponse> onActorJoin(java.lang.String, systems.zlink.framework.messaging.ZLinkMessage);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onJoinedActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onLeaveActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onDisconnectActor(TActor);
}
public final class systems.zlink.framework.kotlin.ZLinkFrameworkExtensionsKt {
  public static final <TMessage> systems.zlink.framework.channels.ZLinkSendCall send(systems.zlink.framework.channels.ZLinkRouteClient, systems.zlink.contracts.core.RoutingId, TMessage);
  public static final <TReply> java.lang.Object request(systems.zlink.framework.channels.ZLinkRouteClient, systems.zlink.contracts.core.RoutingId, systems.zlink.contracts.messaging.Message, kotlin.coroutines.Continuation<? super TReply>);
}
```

Kotlin은 address DTO, process-local handle, resolver, unbounded directory와 direct create/get-or-create terminal
extension을 제공하지 않는다. Direct terminal extension은 fluent option과 single-use state를 숨기기 때문이다.
`close(SpotRef)`는 Missing이면 `false`, generation 불일치는 `SpotGenerationStale`, seal된 이관 구간은
`SpotMoving`으로 처리한다.
