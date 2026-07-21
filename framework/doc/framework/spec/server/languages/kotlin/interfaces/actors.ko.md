# Kotlin Actor 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Actor](../../java/interfaces/actors.ko.md)

Kotlin은 Java factory, transfer cancellation, typed state adapter와 policy를 사용한다. Coroutine 기반 factory는
suspending base class로 제공한다. Factory와 분리된 transfer registry는 제공하지 않는다.
Canonical logical identity와 Actor type 의미도 Java 계약을 그대로 사용한다. Directory는 `(MeshName, ActorId)`
existing-only lookup이며 local create·get-or-create만 explicit Actor type을 받는다. Kotlin wrapper가 type 없는
ensure나 remote creation을 추가하지 않는다.

## Kotlin source signature

아래 선언은 application이 직접 구현하거나 호출하는 Kotlin source 계약이다. `protected` suspending member도
subclass가 구현해야 하므로 계약에 포함한다.

```kotlin
interface ZLinkSuspendingEntrySpotActorSendHandler<
    TEntrySpot : ZLinkEntrySpot<*>,
    TActor : ZLinkActor,
    TMessage,
> {
    suspend fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
    )
}

interface ZLinkSuspendingEntrySpotActorRequestHandler<
    TEntrySpot : ZLinkEntrySpot<*>,
    TActor : ZLinkActor,
    TRequest,
    TReply,
> {
    suspend fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
    ): TReply
}

interface ZLinkSuspendingSpotActorSendHandler<
    TSpot : ZLinkSpot<*>,
    TActor : ZLinkActor,
    TMessage,
> {
    suspend fun handle(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
    )
}

interface ZLinkSuspendingSpotActorRequestHandler<
    TSpot : ZLinkSpot<*>,
    TActor : ZLinkActor,
    TRequest,
    TReply,
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
    meshName: String,
    actorRef: ActorRef,
    request: Any,
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkActorClient.requestToActorAwait(
    meshName: String,
    actorRef: ActorRef,
    request: Any,
): TReply

suspend fun ZLinkActorDirectory.findActor(meshName: String, actorId: String): ActorRef?

fun ActorRef.snapshot(): ActorRefSnapshot
fun ActorRefSnapshot.actorRef(): ActorRef

suspend fun ZLinkActorJoinCall.awaitJoin(): ZLinkActorJoinResult<Void>
suspend fun <TReply> ZLinkActorJoinCall.awaitJoin(
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply>
inline suspend fun <reified TReply> ZLinkActorJoinCall.awaitJoinReply():
    ZLinkActorJoinResult<TReply>
suspend fun ZLinkActorJoinCall.yieldJoin(): ZLinkActorJoinResult<Void>
suspend fun <TReply> ZLinkActorJoinCall.yieldJoin(
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply>
inline suspend fun <reified TReply> ZLinkActorJoinCall.yieldJoinReply():
    ZLinkActorJoinResult<TReply>

suspend fun <T> ZLinkWorkerCall<T>.yieldWorker(): T
```

Typed Snapshot maintenance는 Java `ZLinkTransferStateAdapter<TInstance, TState>`와 아래 reified registration을
사용한다. Policy는 factory registration에 연결되며 별도 adapter registration 단계가 없다.

같은 `stateContractId`의 source와 target adapter는 Java의 `frameworkJsonV1` semantic profile을 공유한다.
Enum string, 64-bit integer decimal string, padded base64, unknown field 무시와 duplicate·required field 검증을
그대로 적용하며 application JSON byte 배열을 canonical representation으로 보장하지 않는다. Canonical byte
identity는 Framework 내부 manifest, chunk와 envelope에만 적용하고 Kotlin codec API를 추가하지 않는다.

Target `ACTIVATED` 뒤에도 application과 session ingress는 sealed 상태이며 restore, replay와 bound-session
route만 staged 상태로 준비한다. Source cleanup terminal과 authority `COMPLETED` CAS 뒤에만 `READY` ingress를
열고 checkpoint fence를 해제한다. 이후 failure는 ordinary owner loss이며 이전 checkpoint를 transparent
replay하지 않는다. Kotlin public phase control은 제공하지 않는다.

Target replacement가 발생하면 stable transfer 안의 각 attempt가 factory와 Java `restore(...)`를 at-least-once
호출할 수 있고 중단된 stale attempt callback이 successor와 겹칠 수 있다. `capture(...)`도 immutable checkpoint
root가 authority에 연결되기 전까지 반복될 수 있다. Current exact owner와 attempt fence만 completion을 commit하고
admission을 연다. Callback에 transfer ID를 추가하지 않으므로 restoration은 retry-safe해야 하며 exactly-once
external side effect를 보장하지 않는다.

Transferred terminal reply accounting은 공유 JVM runtime의 internal command ID 46 `replyRelayAck`를 사용한다.
Stable transfer ID, operation ID, exact request-source fence와 status만 전달하고 payload·metadata는 전달하지 않는다.
Physical connection close는 terminal 증거가 아니며 ACK 또는 accepted record의 exact request-source lease expiry만
terminal accounting을 완료한다. Kotlin public ACK API는 제공하지 않는다.

Connection-bound one-way를 포함해 source가 admission한 모든 connection-bound work는 terminal accounting 뒤에만
`CAPTURED`를 commit한다. Durable journal은 exact owner lease가 있는 source에서만 사용하며 pre-`CAPTURED`
deadline 실패는 abort와 `BLOCKED/TRANSFER_DISABLED`다. 미완료 one-way capture 예외는 없다.

Transferable Actor는 source Entry Spot member여야 한다. User Spot member이면 Retire preflight가
`BLOCKED/TRANSFER_DISABLED`이고 authority와 admission을 바꾸지 않는다. `NEW_OWNER` CAS는 owner, authority
owner generation과 current Spot을 target Entry identity로 원자적으로 바꾼다. Target factory·restore,
`onJoinedActor`, replay 뒤 source `onLeaveActor`와 old Entry removal을 durable cleanup으로 수행한다. Callback은
retry-safe해야 하며 at-least-once 호출될 수 있다. Kotlin public phase API는 없다.

새 distributed Actor는 internal `CREATING` row, final `ActorRef.generation`, factory, initial Entry membership과
initialize 뒤 `READY` CAS를 거친다. Resolver와 remote messaging은 `READY`만 사용한다. 실패하면 exact fence로
delete하고 read로 reconcile하며 확인 전 같은 typed failure와 hidden retry 0을 적용한다. `MISSING` 뒤 다음
caller만 새 create를 시작한다. Entry initialization도 Host `SERVING` publication보다 먼저 완료한다.

```kotlin
inline fun <reified TActor, reified TFactory> ZLinkMeshNodeBuilder.actorFactory(
    actorType: String,
    transfer: ZLinkTransferPolicy<TActor> = ZLinkTransferPolicy.disabled()
): ZLinkMeshNodeBuilder
    where TActor : ZLinkActor,
          TFactory : ZLinkActorFactory =
    addActorFactory(
        actorType,
        TActor::class.java,
        TFactory::class.java,
        transfer
    )

inline fun <TInstance, reified TState, reified TAdapter> snapshotTransfer(
    stateContractId: String
): ZLinkTransferPolicy<TInstance>
    where TAdapter : ZLinkTransferStateAdapter<TInstance, TState> =
    ZLinkTransferPolicy.snapshot(
        stateContractId,
        TState::class.java,
        TAdapter::class.java
    )
```

## Exact generated JVM signature

아래 JVM signature는 Kotlin source contract의 generated form이다.

```java
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory implements systems.zlink.framework.actors.ZLinkActorFactory {
  public systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory();
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActor> create(java.lang.String, systems.zlink.framework.actors.ZLinkActorContext);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler<TEntrySpot extends systems.zlink.framework.spots.ZLinkEntrySpot<?>, TActor extends systems.zlink.framework.actors.ZLinkActor, TRequest, TReply> {
  public abstract java.lang.Object handle(TEntrySpot, TActor, systems.zlink.framework.spots.ZLinkSpotActorRequestContext, TRequest, kotlin.coroutines.Continuation<? super TReply>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorSendHandler<TEntrySpot extends systems.zlink.framework.spots.ZLinkEntrySpot<?>, TActor extends systems.zlink.framework.actors.ZLinkActor, TMessage> {
  public abstract java.lang.Object handle(TEntrySpot, TActor, systems.zlink.framework.spots.ZLinkSpotActorSendContext, TMessage, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler<TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>, TActor extends systems.zlink.framework.actors.ZLinkActor, TRequest, TReply> {
  public abstract java.lang.Object handle(TSpot, TActor, systems.zlink.framework.spots.ZLinkSpotActorRequestContext, TRequest, kotlin.coroutines.Continuation<? super TReply>);
}
public interface systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorSendHandler<TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>, TActor extends systems.zlink.framework.actors.ZLinkActor, TMessage> {
  public abstract java.lang.Object handle(TSpot, TActor, systems.zlink.framework.spots.ZLinkSpotActorSendContext, TMessage, kotlin.coroutines.Continuation<? super kotlin.Unit>);
}
```
