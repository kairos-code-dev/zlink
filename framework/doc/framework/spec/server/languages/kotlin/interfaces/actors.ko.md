# Kotlin Actor 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Actor](../../java/interfaces/actors.ko.md) ·
[Actor 공통 계약](../../../22-actor-model.ko.md)

Kotlin은 Java의 global Actor identity와 fluent operation을 그대로 사용한다. `ActorId`는 Location Store
transaction domain 전체에서 유일하며 UTF-8 encoded 크기는 1..255 bytes다. 대소문자를 구분하고
normalization하지 않는다. 일반 send/request는 ActorId만 받으며 current authority를 resolve한다.
`ActorRef(actorId, objectGeneration, meshName, nodeRid)`는 exact incarnation을 destroy하거나 session에
bind할 때만 사용한다. `objectGeneration`은 `1..Long.MAX_VALUE`이고 JSON에서는 decimal string이다.

`ZLinkActorManager.create(actorId, actorType)`와 `getOrCreate(actorId, actorType)`는 Java의 single-use fluent
call을 반환한다. `inMesh`, `request`, `placementProfile`, `affinityKey`, `timeout`을 설정한 뒤 terminal
`submit`을 한 번만 호출한다. 같은 option을 두 번 설정하면 `InvalidConfiguration`, 두 번째 submit은
`AlreadySubmitted`다. `inMesh`를 생략했을 때 object role Mesh가 하나면 자동 선택하고, 0개면
`ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`다. 지정한 Mesh가 없으면 `MeshNotFound`다.
`placementProfile`과 `affinityKey`는 각각 UTF-8 1..255 bytes이며 target RID나 predicate callback을 받는
placement API는 제공하지 않는다.

Actor type은 UTF-8 1..255 bytes의 stable exact value다. `Create`에서 Ready object가 있으면
`ActorAlreadyExists`, `GetOrCreate`에서 같은 type의 Ready 또는 Creating object가 있으면 같은 attempt에
합류한다. 다른 type이면 `ActorTypeMismatch`다. Kotlin은 local Actor create, directory, resolver 또는
hidden remote retry를 추가하지 않는다.

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
suspend fun ZLinkActorJoinCall.yieldJoin(): ZLinkActorJoinResult<Void>
suspend fun <TReply> ZLinkActorJoinCall.yieldJoin(
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply>
inline suspend fun <reified TReply> ZLinkActorJoinCall.yieldJoinReply():
    ZLinkActorJoinResult<TReply>

suspend fun <T> ZLinkWorkerCall<T>.yieldWorker(): T

inline fun <TInstance, reified TState, reified TAdapter> snapshotTransfer(
    stateContractId: String,
): ZLinkTransferPolicy<TInstance>
    where TAdapter : ZLinkTransferStateAdapter<TInstance, TState>
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
  public static final <TInstance, TState, TAdapter extends systems.zlink.framework.actors.ZLinkTransferStateAdapter<TInstance, TState>> systems.zlink.framework.actors.ZLinkTransferPolicy<TInstance> snapshotTransfer(java.lang.String);
}
```

Factory registration은 `Disabled`, `Recreate`, `Snapshot` 중 하나를 반드시 받는다. Kotlin reified helper도
policy를 생략하는 overload나 default argument를 생성하지 않는다. Exact `ActorRef`를 받는 public operation은
destroy와 session bind뿐이다. Missing exact ref는 `false`, generation 불일치는 `ActorGenerationStale`, seal된
이관 구간은 `ActorMoving`으로 처리한다.
