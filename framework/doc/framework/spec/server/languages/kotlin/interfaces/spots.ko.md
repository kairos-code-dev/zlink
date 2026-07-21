# Kotlin Spot 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Spot](../../java/interfaces/spots.ko.md)

Instance Spot은 Java builder를 직접 사용하므로 같은 인자를 전달하는 Kotlin wrapper를 추가하지 않는다.
User Spot에는 transfer policy가 없으며, Instance factory에서 policy를 생략하면 `Disabled`다. Kotlin address
extension도 public local-only·existing-only 의미를 바꾸거나 hidden remote `GetOrCreate`를 시작하지 않는다.

Store-backed dynamic User Spot은 internal `CREATING` row를 `NEW_OBJECT` CAS로 만든 뒤 factory, configure,
initialize와 `READY` CAS를 수행한다. Resolve와 remote messaging은 `READY`만 사용한다. 실패하면 exact fence로
delete하고 read로 reconcile하며 확인 전 같은 typed failure와 hidden retry 0을 적용한다. `MISSING` 뒤 다음
caller만 새 create를 시작한다. User Spot `close()`는 active Actor membership이나 missing이면 `false`다. Active
membership에서는 state·admission·authority를 바꾸지 않고 closing callback이나 hidden leave·destroy를 실행하지
않는다. Caller가 명시적으로 leave·destroy한 뒤 다시 close하며 Host Shutdown·Retire는 Actor barrier 뒤 Spot을
cleanup한다.

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

suspend inline fun <reified TSpot : ZLinkSpot<*>>
    ZLinkSpotManager.create(meshName: String): ZLinkSpotCreateResult
suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    meshName: String,
    request: ZLinkMessage,
): ZLinkSpotCreateResult
suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    meshName: String,
    spotRid: RoutingId,
): ZLinkSpotCreateResult
suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.getOrCreate(
    meshName: String,
    spotRid: RoutingId,
): ZLinkSpotCreateResult
suspend inline fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.getOrCreate(
    meshName: String,
    spotRid: RoutingId,
    request: ZLinkMessage,
): ZLinkSpotCreateResult

fun <TMessage> ZLinkRouteClient.send(
    target: SpotHandle,
    message: TMessage,
): ZLinkSendCall
suspend inline fun <reified TReply> ZLinkRouteClient.request(
    target: SpotHandle,
    message: Message,
): TReply
```

Lifecycle의 Java `CompletionStage` override는 base class가 final로 구현한다. 위 `protected` member가 application
subclass의 source 계약이며 coroutine bridge 구현은 공개 계약이 아니다.

Cold address call의 source는 location resolve, eligible target 선택과 `COLD_ACTIVATING` CAS claim을 outbound보다
먼저 같은 send deadline 안에서 완료한다. Target은 source가 확정한 token과 generation을 다시 검증하고 factory
activation과 `READY` CAS만 수행하며 target-side claim을 시작하지 않는다. One-way `submit()` 완료는 source
outbound admission까지 기다리지만 target factory 실행, activation queue 수락과 `READY`는 기다리지 않는다.

Cold Instance factory·initialize failure는 durable public `FAILED` state를 만들지 않는다. Runtime은 local failed
barrier와 exact fenced delete/read reconcile을 사용한다. Delete 확인 전 같은 typed failure와 hidden retry 0을
적용하고 `MISSING` 확인 뒤 다음 caller만 새 `COLD_ACTIVATING`을 시작한다. Kotlin public recovery API는 없다.

```kotlin
fun <TMessage> ZLinkRouteClient.send(
    target: InstanceSpotAddress,
    message: TMessage,
): ZLinkSendCall

fun <TMessage> ZLinkRouteClient.request(
    target: InstanceSpotAddress,
    message: TMessage,
): ZLinkRequestCall

fun <TMessage> ZLinkSpotOutbound.send(
    target: InstanceSpotAddress,
    message: TMessage,
): ZLinkSendCall

fun <TMessage> ZLinkSpotOutbound.request(
    target: InstanceSpotAddress,
    message: TMessage,
): ZLinkRequestCall
```

## Exact generated JVM signature

아래 JVM signature는 Kotlin source contract의 generated form이다.

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
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSpotPacketHandler<TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>, TMessage> {
  public abstract java.lang.Object handle(TSpot, TMessage, kotlin.coroutines.Continuation<? super kotlin.Unit>);
  public default java.lang.Object handle(TSpot, TMessage, systems.zlink.framework.channels.ZLinkSendContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
  public static <TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>, TMessage> java.lang.Object handle$suspendImpl(systems.zlink.framework.kotlin.ZLinkSuspendingSpotPacketHandler<TSpot, TMessage>, TSpot, TMessage, systems.zlink.framework.channels.ZLinkSendContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public final class systems.zlink.framework.kotlin.ZLinkSuspendingSpotPacketHandler$DefaultImpls {
  public static <TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>, TMessage> java.lang.Object handle(systems.zlink.framework.kotlin.ZLinkSuspendingSpotPacketHandler<TSpot, TMessage>, TSpot, TMessage, systems.zlink.framework.channels.ZLinkSendContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler<TSpot, TRequest, TReply> {
  public abstract java.lang.Object handle(TSpot, TRequest, kotlin.coroutines.Continuation<? super TReply>);
  public default java.lang.Object handle(TSpot, TRequest, systems.zlink.framework.channels.ZLinkRequestContext, kotlin.coroutines.Continuation<? super TReply>);
  public static <TSpot, TRequest, TReply> java.lang.Object handle$suspendImpl(systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler<TSpot, TRequest, TReply>, TSpot, TRequest, systems.zlink.framework.channels.ZLinkRequestContext, kotlin.coroutines.Continuation<? super TReply>);
}
public final class systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler$DefaultImpls {
  public static <TSpot, TRequest, TReply> java.lang.Object handle(systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler<TSpot, TRequest, TReply>, TSpot, TRequest, systems.zlink.framework.channels.ZLinkRequestContext, kotlin.coroutines.Continuation<? super TReply>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler<TSpot, TEvent> {
  public abstract java.lang.Object handle(TSpot, TEvent, kotlin.coroutines.Continuation<? super kotlin.Unit>);
  public default java.lang.Object handle(TSpot, TEvent, systems.zlink.framework.channels.ZLinkPublishContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
  public static <TSpot, TEvent> java.lang.Object handle$suspendImpl(systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler<TSpot, TEvent>, TSpot, TEvent, systems.zlink.framework.channels.ZLinkPublishContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public final class systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler$DefaultImpls {
  public static <TSpot, TEvent> java.lang.Object handle(systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler<TSpot, TEvent>, TSpot, TEvent, systems.zlink.framework.channels.ZLinkPublishContext, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSpotTimerHandler<TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>> {
  public abstract java.lang.Object handle(TSpot, systems.zlink.framework.spots.ZLinkTimerTick, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
```
