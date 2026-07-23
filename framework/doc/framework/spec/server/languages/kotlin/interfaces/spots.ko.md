# Kotlin Spot 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Spot](../../java/interfaces/spots.ko.md) ·
[Spot 공통 계약](../../../23-spot-actor.ko.md)

SpotRid는 Location Store transaction domain 전체에서 유일한 logical ID다. 일반 Spot send/request는
SpotRid만 받는다. `SpotRef(spotRid, objectGeneration, meshName, nodeRid)`는 exact incarnation을 close할 때만
사용하는 immutable snapshot이다. `objectGeneration`은 `1..Long.MAX_VALUE`이고 JSON에서는 decimal string이다.
User와 Instance Spot type은 UTF-8 1..255 bytes의 stable exact value다.
Java enum의 numeric value는 `ZLinkSpotKind.INVALID=0`, `ENTRY=1`, `USER=2`, `INSTANCE=3`이고
Kotlin은 ordinal을 계약 값으로 사용하지 않고 `value()`를 사용한다. Creatable kind enum은 제공하지 않는다.

`ZLinkSpotManager.create(spotType)`은 User Spot RID를 생성하고,
`getOrCreate(spotRid, spotType)`은 caller가 정한 User Spot RID를 사용한다. Manager는 Instance Spot
create/get-or-create를 제공하지 않는다. 두 operation은 Java single-use fluent call을 반환하며 `inMesh`,
`request`, `placementProfile`, `affinityKey`, `timeout` 뒤 terminal `submit`을 정확히 한 번 호출한다. 중복
option과 중복 submit, Mesh 선택, type 충돌과 deadline 규칙은 Actor operation과 같다. Entry Spot RID는
Framework가 만들며 public create 대상이 아니다.

Spot send/request는 global SpotRid만 address로 받고 Java의 `ZLinkSpotSendCall` 또는 `ZLinkSpotRequestCall`을
반환한다. `instanceSpot()`이나 `instanceSpot(stableType)`을 호출한 call만 Missing Instance Spot의 cold
activation intent를 만든다. Marker가 없으면 Missing authority는 not-found다. Existing authority가 있으면
등록 type 수와 관계없이 저장된 stable type을 사용하므로 type을 다시 요구하지 않는다.

Missing authority에 `instanceSpot()`을 사용하면 placement가 선택한 Mesh의 serving Instance type이 distinct
value 하나일 때만 그 type을 자동 선택한다. `inMesh`를 지정하면 그 Mesh가 type 선택 범위가 되며, 두 개
이상이면 `instanceSpot(stableType)`이 필요하다. Stable type 인자는 Missing cold activation에만 사용하고
existing authority를 resolve하는 데는 필요하지 않다. Caller가 명시한 type과 stored type이 다르면
`SpotTypeMismatch`다.
`inMesh`, `placementProfile`과 `affinityKey`는 Missing cold activation placement에만 적용하며 existing owner를
재배치하지 않는다. Kotlin은 이 fluent state를 숨기는 terminal request extension을 제공하지 않는다.

Kotlin은 Java `ZLinkSpotRelocationAdapter<TSpot>`를 그대로 구현한다. Opaque `byte[]`는 `ByteArray`로 보이고
`capture`와 `restore`는 Java 계약과 같은 `CompletionStage`를 반환한다. 별도 suspending Spot adapter,
`TState`, `stateContractId`, state class와 `ZLinkMessage` relocation surface는 제공하지 않는다. Snapshot policy는
`ZLinkRelocationPolicy.snapshot(SpotAdapter::class.java)`를 사용하고 factory target과 adapter type은 socket bind
전에 검증한다.

Snapshot whole User Spot relocation은 Spot 자체에 Spot adapter를, member Actor마다 Actor adapter를 사용한다.
Snapshot Instance Spot relocation은 Spot adapter를 사용한다. Same-node operation, `Disabled`와 `Recreate`에서는
adapter를 호출하지 않는다. Capture `ByteArray`는 최대 64 MiB이며 adapter가 completion까지 소유한다. Java
runtime은 completion에서 복사한다.
Restore는 호출마다 fresh defensive copy를 받고 completion 뒤 보관하지 않는다. Empty `ByteArray`도 유효한
Snapshot state다. Factory는 target attempt마다 fresh Spot instance를 만들며 source나 이전 attempt instance를
재사용하지 않는다. 같은 attempt의 restore는 반복될 수 있다. Capture exception은 source authority와 admission을
유지하고 restore exception은 target을 sealed 상태로 유지한 채 same payload retry 또는 target replacement로
처리한다. Null stage와 null capture payload는 contract 위반이다. Host Retire의 precommit adapter
exception·contract violation은 deadline이 먼저 확정되지 않았으면 `Blocked/StateIncompatible`, deadline이 먼저
확정되면 `Blocked/DeadlineExceeded`다. Stale attempt cancellation은 terminal result를 commit하지 못한다.
Callback은 at-least-once이고 stale attempt와 겹칠 수 있으므로 retry-safe해야 한다.

Spot closing reason은 Java의 `ZLinkSpotCloseReason`을 사용하며 값은 `EXPLICIT_CLOSE=0`, `HOST_SHUTDOWN=1`,
`RELOCATION_OUT=2`다. `ZLinkSpotClosingContext.deadline`은 absolute `Instant`다. Java lifecycle interface는
context만 받고 별도 Framework cancellation 타입을 사용하지 않는다. Suspending projection은 cleanup deadline에
bridge coroutine을 cancel하며 callback은 coroutine cancellation을 그대로 따른다. Actor별 closing callback은
제공하지 않는다.

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

// Relocation은 logical timer와 pending tick을 Framework payload로 복원한다.

abstract class ZLinkSuspendingSpot<TActor : ZLinkActor> : ZLinkSpot<TActor> {
    abstract override fun context(): ZLinkSpotContext
    protected open suspend fun onCreateSuspending(
        request: ZLinkMessage,
    ): ZLinkSpotCreateResponse
    protected open suspend fun onInitializeSuspending()
    protected open suspend fun onClosingSuspending(
        context: ZLinkSpotClosingContext,
    )
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
    protected open suspend fun onClosingSuspending(
        context: ZLinkSpotClosingContext,
    )
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
    protected open suspend fun onActorRelocatedSuspending(actor: TActor)
}

inline fun <reified THandler : Any> ZLinkSpotHandlerRegistry.addHandler()

fun <TMessage> ZLinkRouteClient.send(
    spotRid: RoutingId,
    message: TMessage,
): ZLinkSpotSendCall
fun <TRequest> ZLinkRouteClient.request(
    spotRid: RoutingId,
    message: TRequest,
): ZLinkSpotRequestCall
```

User·Instance Spot relocation에서는 Java runtime이 logical timer registration, 마지막 완료 tick sequence, 다음 예정
시각과 아직 실행하지 않은 pending tick을 relocation payload에 포함한다. Target은 새 native timer handle을 만들며
application이 timer를 다시 등록하지 않는다. 현재 실행 중인 suspending timer handler만 source에서 완료하고 target
Ready 전에는 복원한 tick을 실행하지 않는다.

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
  public final java.util.concurrent.CompletionStage<java.lang.Void> onClosing(systems.zlink.framework.spots.ZLinkSpotClosingContext);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onCreateActor(TActor, systems.zlink.framework.messaging.ZLinkMessage);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.spots.ZLinkSpotActorJoinResponse> onActorJoin(java.lang.String, systems.zlink.framework.messaging.ZLinkMessage);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onJoinedActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onLeaveActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onDisconnectActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onActorRelocated(TActor);
}
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingSpot<TActor extends systems.zlink.framework.actors.ZLinkActor> implements systems.zlink.framework.spots.ZLinkSpot<TActor> {
  public systems.zlink.framework.kotlin.ZLinkSuspendingSpot();
  public abstract systems.zlink.framework.spots.ZLinkSpotContext context();
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.spots.ZLinkSpotCreateResponse> onCreate(systems.zlink.framework.messaging.ZLinkMessage);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onInitialize();
  public final java.util.concurrent.CompletionStage<java.lang.Void> onClosing(systems.zlink.framework.spots.ZLinkSpotClosingContext);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.spots.ZLinkSpotActorJoinResponse> onActorJoin(java.lang.String, systems.zlink.framework.messaging.ZLinkMessage);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onJoinedActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onLeaveActor(TActor);
  public final java.util.concurrent.CompletionStage<java.lang.Void> onDisconnectActor(TActor);
}
public final class systems.zlink.framework.kotlin.ZLinkFrameworkExtensionsKt {
  public static final <TMessage> systems.zlink.framework.spots.ZLinkSpotSendCall send(systems.zlink.framework.channels.ZLinkRouteClient, systems.zlink.contracts.core.RoutingId, TMessage);
  public static final <TRequest> systems.zlink.framework.spots.ZLinkSpotRequestCall request(systems.zlink.framework.channels.ZLinkRouteClient, systems.zlink.contracts.core.RoutingId, TRequest);
}
```

Kotlin은 address DTO, process-local handle, resolver, unbounded directory와 User Spot create/get-or-create 또는
Instance messaging의 direct terminal extension을 제공하지 않는다. Direct terminal extension은 fluent option과
single-use state를 숨기기 때문이다.
`close(SpotRef)`는 Missing이면 `false`, generation 불일치는 `SpotGenerationStale`, seal된 이관 구간은
`SpotMoving`으로 처리하며 User Spot만 대상으로 한다. Instance Spot의 self-close는 Java
`ZLinkInstanceSpotContext.close()`를 그대로 사용한다.

`onActorRelocatedSuspending(actor)`는 Java `ZLinkEntrySpot.onActorRelocated(actor)`의 coroutine bridge이며 별도 lifecycle
API가 아니다. Maintenance target은 Actor adapter restore, Location commit, 이 callback과 source Entry Spot의
`onLeaveActorSuspending(actor)`, dispatch 개방 순서로 처리한다. 어느 callback의 exception도 commit을 rollback하지
않고 target을 sealed 상태로 유지한 채 retry한다. 일반
same-node·remote join은 `onActorJoinSuspending`과 `onJoinedActorSuspending`을 사용하며 이 callback을 호출하지
않는다. Maintenance relocation에서는 target의 일반 join callback은 호출하지 않는다. Whole User Spot aggregate에서는
member의 Entry/User Spot membership callback을 모두 호출하지 않는다.
