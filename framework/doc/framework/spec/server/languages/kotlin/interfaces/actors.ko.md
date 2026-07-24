# Kotlin Actor 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Actor](../../java/interfaces/actors.ko.md) ·
[Actor 공통 계약](../../../22-actor-model.ko.md)

Kotlin은 Java의 global Actor identity와 fluent operation을 그대로 사용한다. `ActorId`는 Location Store
transaction domain 전체에서 유일하며 UTF-8 encoded 크기는 1..255 bytes다. 대소문자를 구분하고
normalization하지 않는다. 일반 send/request는 ActorId만 받으며 current authority를 resolve한다.
`ActorRef(actorId, objectGeneration, meshName, nodeRid)`는 exact incarnation을 destroy하거나 session에
bind할 때만 사용한다. `objectGeneration`은 `1..Long.MAX_VALUE`이고 JSON에서는 decimal string이다.

`ZLinkActorManager.create(actorId, actorType)`와 `getOrCreate(actorId, actorType)`는 Java의 single-use fluent
call을 반환한다. `inMesh`, `request`, `timeout`을 설정한 뒤 terminal
`submit`을 한 번만 호출한다. 같은 option을 두 번 설정하면 `InvalidConfiguration`, 두 번째 submit은
`AlreadySubmitted`다. `inMesh`를 생략했을 때 object role Mesh가 하나면 자동 선택하고, 0개면
`ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`다. 지정한 Mesh가 없으면 `MeshNotFound`다.
Target RID나 predicate callback을 받는
placement API는 제공하지 않는다.

Actor type은 UTF-8 1..255 bytes의 stable exact value다. `Create`에서 Ready object가 있으면
`ActorAlreadyExists`, `GetOrCreate`에서 같은 type의 Ready object가 있으면 `Existing`을 반환한다. Creating
object가 있으면 authority 변경을 기다리고 Ready면 `Existing`, rejection cleanup이면 새 reservation으로
자신의 request를 실행한다. 서로 다른 operation은 앞선 `Rejected` reply를 공유하지 않고 같은 operation ID
retry만 terminal result를 재사용한다. 다른 type이면 `ActorTypeMismatch`다. Kotlin은 local Actor create, directory, resolver 또는
hidden remote retry를 추가하지 않는다.

Kotlin은 Java `ZLinkActorRelocationAdapter<TActor>`와 `ZLinkRelocationPolicy<TInstance>`를 그대로 사용한다.
Opaque Java `byte[]`는 Kotlin `ByteArray`로 보이며 `capture`와 `restore`의 asynchronous completion은
`CompletionStage`다. 별도 suspending adapter, `TState`, `stateContractId`, state class와 `ZLinkMessage` 기반
relocation API를 만들지 않는다. Snapshot policy는
`ZLinkRelocationPolicy.snapshot(ActorAdapter::class.java)`로 구성하며 factory와 adapter target의 일치는 socket
bind 전에 검증한다. Java interop에서 null adapter class를 전달한 policy도 bind 전에 `InvalidConfiguration`으로
거부한다.

Snapshot Actor adapter는 maintenance cross-node materialization, remote User·Entry Spot join과 whole User Spot
relocation의 각 Actor participant에 사용한다. Same-node join, `Disabled`와 `Recreate`에서는 호출하지 않는다.
Capture가 반환한 `ByteArray`는 최대 64 MiB이며 adapter가 completion까지 소유한다. Java runtime은 completion에서
복사한다. Restore는 호출마다
fresh defensive copy를 받고 completion 뒤 보관하지 않는다. Empty `ByteArray`도 유효한 Snapshot state다.
Factory는 target attempt마다 fresh Actor instance를 만들며 source나 이전 attempt instance를 재사용하지 않는다.
같은 attempt의 restore는 반복될 수 있다. Capture exception은 source authority와 admission을 유지하고, restore
exception은 target을 sealed 상태로 유지한 채 same payload retry 또는 target replacement로 처리한다. Null stage와
null capture payload는 contract 위반이다. Host Retire의 precommit adapter exception·contract violation은 deadline이
먼저 확정되지 않았으면 `Blocked/StateIncompatible`, deadline이 먼저 확정되면 `Blocked/DeadlineExceeded`다.
Stale attempt cancellation은 terminal result를 commit하지 못한다. 두 callback은 at-least-once이고 stale attempt와
겹칠 수 있으므로 retry-safe해야 한다. Kotlin coroutine 안에서 exception을 정상 completion으로 바꾸거나 empty
`ByteArray`를 failure fallback으로 반환하지 않는다.

## Kotlin source signature

```kotlin
interface ZLinkSuspendingEntrySpotActorSendHandler<
    TEntrySpot : ZLinkEntrySpot<*>, TActor : ZLinkActor, TMessage,
> {
    suspend fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
    )
}

interface ZLinkSuspendingEntrySpotActorRequestHandler<
    TEntrySpot : ZLinkEntrySpot<*>, TActor : ZLinkActor, TRequest, TReply,
> {
    suspend fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
    ): TReply
}

interface ZLinkSuspendingSpotActorSendHandler<
    TSpot : ZLinkSpot<*>, TActor : ZLinkActor, TMessage,
> {
    suspend fun handle(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
    )
}

interface ZLinkSuspendingSpotActorRequestHandler<
    TSpot : ZLinkSpot<*>, TActor : ZLinkActor, TRequest, TReply,
> {
    suspend fun handle(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
    ): TReply
}

abstract class ZLinkSuspendingActorFactory : ZLinkActorFactory {
    protected abstract suspend fun createActor(
        actorId: String,
        context: ZLinkActorContext,
    ): ZLinkActor
}

suspend fun <TReply> ZLinkActorRequestCall.awaitReply(
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkActorRequestCall.awaitReply(): TReply
suspend fun <TReply> ZLinkActorRequestCall.yieldReply(
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkActorRequestCall.yieldReply(): TReply

suspend fun <TReply> ZLinkActorClient.requestToActorAwait(
    actorId: String,
    request: Any,
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkActorClient.requestToActorAwait(
    actorId: String,
    request: Any,
): TReply

suspend fun ZLinkActorJoinCall.awaitJoin(): ZLinkActorJoinResult<Void>
suspend fun <TReply> ZLinkActorJoinCall.awaitJoin(
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply>
inline suspend fun <reified TReply> ZLinkActorJoinCall.awaitJoinReply():
    ZLinkActorJoinResult<TReply>

suspend fun <T> ZLinkWorkerCall<T>.yieldWorker(): T
```

## Exact generated JVM signature

```java
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory implements systems.zlink.framework.actors.ZLinkActorFactory {
  public systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory();
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActor> create(java.lang.String, systems.zlink.framework.actors.ZLinkActorContext);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorSendHandler<TEntrySpot extends systems.zlink.framework.spots.ZLinkEntrySpot<?>, TActor extends systems.zlink.framework.actors.ZLinkActor, TMessage> {
  public abstract java.lang.Object handle(TEntrySpot, TActor, systems.zlink.framework.spots.ZLinkSpotActorSendContext, TMessage, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler<TEntrySpot extends systems.zlink.framework.spots.ZLinkEntrySpot<?>, TActor extends systems.zlink.framework.actors.ZLinkActor, TRequest, TReply> {
  public abstract java.lang.Object handle(TEntrySpot, TActor, systems.zlink.framework.spots.ZLinkSpotActorRequestContext, TRequest, kotlin.coroutines.Continuation<? super TReply>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorSendHandler<TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>, TActor extends systems.zlink.framework.actors.ZLinkActor, TMessage> {
  public abstract java.lang.Object handle(TSpot, TActor, systems.zlink.framework.spots.ZLinkSpotActorSendContext, TMessage, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler<TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>, TActor extends systems.zlink.framework.actors.ZLinkActor, TRequest, TReply> {
  public abstract java.lang.Object handle(TSpot, TActor, systems.zlink.framework.spots.ZLinkSpotActorRequestContext, TRequest, kotlin.coroutines.Continuation<? super TReply>);
}
public final class systems.zlink.framework.kotlin.ZLinkFrameworkExtensionsKt {
  public static final <TReply> java.lang.Object requestToActorAwait(systems.zlink.framework.actors.ZLinkActorClient, java.lang.String, java.lang.Object, java.lang.Class<TReply>, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object requestToActorAwait(systems.zlink.framework.actors.ZLinkActorClient, java.lang.String, java.lang.Object, kotlin.coroutines.Continuation<? super TReply>);
}
```

Factory registration은 `Disabled`, `Recreate`, `Snapshot` 중 하나를 반드시 받는다. Kotlin은 Snapshot policy와
adapter registration을 위한 reified helper, policy를 생략하는 overload와 default argument를 생성하지 않는다.
Exact `ActorRef`를 받는 public operation은
destroy와 session bind뿐이다. Missing exact ref는 `false`, generation 불일치는 `ActorGenerationStale`, seal된
이관 구간은 `ActorMoving`으로 처리한다.

Actor join extension은 `awaitJoin`만 제공하고 `yieldJoin` 계열은 제공하지 않는다. `yieldReply`와
`yieldWorker`는 `SPOT_WIDE` User Spot member Actor에서 Actor FIFO claim을 유지하고 User Spot gate만
반환한다. Entry Actor와 `PER_ACTOR` Actor에서는 underlying Java operation submission 전에
`InvalidConfiguration`으로 완료한다. 같은 Actor 자신에게 보내는 awaited request도 coroutine을 suspend하거나
queue를 변경하기 전에 거부한다.
`SPOT_WIDE` member Actor가 현재 User Spot을 떠나는 `joinSpot(...).awaitJoin(...)`을 기다리는 경우도 source
lifecycle callback이 같은 gate를 얻어야 하므로 underlying Java operation을 제출하거나 coroutine을
suspend하고 source·target queue를 변경하기 전에 `InvalidConfiguration`으로 완료한다. Callback을 inline
또는 재진입 방식으로 호출하지 않는다.
