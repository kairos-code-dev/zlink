# ZLink Framework Kotlin Handler Contract

이 문서는 `zlink-framework-kotlin`이 추가하는 suspending handler와 lifecycle adapter의
정식 시그니처를 고정한다. 사용법은 Kotlin guide에서 설명하며, Kotlin 구현과 contract
test는 이 문서의 시그니처를 따라야 한다.
본문 선언은 ZLink Framework 10.0.0의 정식 공개 계약이다.

Java interface를 그대로 사용하는 표면은
[Java interface 계약](../java/02-handler-interfaces.ko.md)을 따른다. 아래 Kotlin 타입의
package는 `systems.zlink.framework.kotlin`이다.

Framework 실패는 Java의 `ZLinkFrameworkErrorKind`와 `ZLinkFrameworkException`을 그대로 사용한다. Kotlin은
같은 의미의 enum이나 exception을 다시 선언하지 않는다. 숫자 값과 기본 재시도 의미는
[Java Framework 오류](../java/02-handler-interfaces.ko.md#41-framework-오류)와
[공통 Framework API §13](../../../05-framework-api.ko.md#13-오류-kind)을 따른다.
Request 대기가 timeout, coroutine cancellation 또는 host shutdown으로 끝난 경우도 Java의
`ZLinkRequestFailureException`과 `ZLinkRequestFailureReason`을 그대로 사용한다. Kotlin coroutine adapter는
세 원인을 각각 `TIMEOUT`, `CANCELLED`, `SHUTDOWN`으로 보존하며 `REQUEST_FAILED`로 합치지 않는다.
Actor와 bound session 사이 metadata 전달 정책도 Java의 방향별
`allowSessionToActor(key)`와 `allowActorToSession(key)` 표면을 그대로 사용한다.

## 1. Channel과 Spot handler

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

interface ZLinkSuspendingSpotPacketHandler<TSpot : ZLinkSpot<*>, TMessage> {
    suspend fun handle(spot: TSpot, message: TMessage)
}

interface ZLinkSuspendingSpotRequestHandler<TSpot : Any, TRequest, TReply> {
    suspend fun handle(spot: TSpot, request: TRequest): TReply
}

interface ZLinkSuspendingSpotSubscriptionHandler<TSpot : Any, TEvent> {
    suspend fun handle(spot: TSpot, event: TEvent)
}

interface ZLinkSuspendingSpotTimerHandler<TSpot : ZLinkSpot<*>> {
    suspend fun handle(spot: TSpot, tick: ZLinkTimerTick)
}
```

## 2. Spot actor handler

Spot actor callback은 `suspend` 함수로 완료를 표현하며 별도 cancellation token을
받지 않는다. coroutine 종료는 호출한 coroutine의 lifecycle을 따른다.

```kotlin
interface ZLinkSuspendingEntrySpotActorSendHandler<
    TActor : ZLinkActor,
    TMessage,
> {
    suspend fun handle(
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
    )
}

interface ZLinkSuspendingEntrySpotActorRequestHandler<
    TActor : ZLinkActor,
    TRequest,
    TReply,
> {
    suspend fun handle(
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
    ): TReply
}

interface ZLinkSuspendingSpotActorSendHandler<
    TActor : ZLinkActor,
    TMessage,
> {
    suspend fun handle(
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
    )
}

interface ZLinkSuspendingSpotActorRequestHandler<
    TActor : ZLinkActor,
    TRequest,
    TReply,
> {
    suspend fun handle(
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
    ): TReply
}
```

Actor payload handler는 mutable owner로 Actor 하나만 받는다. 현재 Spot membership은 Actor context의
읽기 전용 snapshot으로 확인하며 Spot 소유 상태 변경은 `SpotHandle` direct call로 제출한다.

## 3. Session packet handler

```kotlin
interface ZLinkSuspendingTypedSessionPacketHandler<
    TSessionContext : ZLinkSessionContext,
    TMessage : Any,
> {
    fun messageType(): Class<TMessage>

    suspend fun handle(
        context: TSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        message: TMessage,
    )
}
```

## 4. Actor adapter

아래 `protected` suspending member도 사용자가 구현하는 subclass 계약이므로 정식
시그니처에 포함한다.
Base class는 Java interface가 요구하는 `CompletionStage` callback을 final로 구현하고 대응하는
`suspend` member를 비차단 방식으로 실행한다. Application subclass에는 아래 public·protected member만
노출하며 blocking adapter를 요구하지 않는다.

```kotlin
abstract class ZLinkSuspendingActorFactory : ZLinkActorFactory {
    protected abstract suspend fun createActor(
        actorId: String,
        context: ZLinkActorContext,
    ): ZLinkActor
}

abstract class ZLinkSuspendingActorTransferAdapter<TActor : ZLinkActor> :
    ZLinkActorTransferAdapter<TActor> {
    protected abstract suspend fun transferOutSuspending(actor: TActor): ZLinkMessage

    protected abstract suspend fun transferInSuspending(
        actorId: String,
        context: ZLinkActorContext,
        state: ZLinkMessage,
    ): TActor
}
```

## 5. Spot lifecycle adapter

```kotlin
abstract class ZLinkSuspendingSpot<TActor : ZLinkActor> : ZLinkSpot<TActor> {
    abstract override fun context(): ZLinkSpotContext

    // 기본 구현이 있다(accept). 필요할 때만 재정의한다.
    protected open suspend fun onCreateSuspending(
        request: ZLinkMessage,
    ): ZLinkSpotCreateResponse

    protected open suspend fun onInitializeSuspending()
    protected open suspend fun onClosingSuspending()

    protected abstract suspend fun onActorJoinSuspending(
        actor: ZLinkActorJoinRequest,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse

    protected abstract suspend fun onJoinedActorSuspending(actor: ZLinkActorMembership)

    protected abstract suspend fun onLeaveActorSuspending(actor: ZLinkActorMembership)
    protected open suspend fun onDisconnectActorSuspending(actor: ZLinkActorMembership)
}

abstract class ZLinkSuspendingEntrySpot<TActor : ZLinkActor> :
    ZLinkEntrySpot<TActor> {
    abstract override fun context(): ZLinkEntrySpotContext

    protected open suspend fun onInitializeSuspending()
    protected open suspend fun onClosingSuspending()

    protected abstract suspend fun onCreateActorSuspending(
        actor: ZLinkActorMembership,
        createRequest: ZLinkMessage,
    )

    protected abstract suspend fun onActorJoinSuspending(
        actor: ZLinkActorJoinRequest,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse

    protected abstract suspend fun onJoinedActorSuspending(actor: ZLinkActorMembership)

    protected abstract suspend fun onLeaveActorSuspending(actor: ZLinkActorMembership)
    protected open suspend fun onDisconnectActorSuspending(actor: ZLinkActorMembership)
}
```

## 6. Session lifecycle adapter

```kotlin
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
```

위 선언은 application subclass가 구현하거나 재정의할 public·protected member만 고정한다. 상속한 Java
callback은 base class가 final로 구현하며 대응하는 suspending member를 비차단 방식으로 호출한다. Coroutine을
`CompletionStage`로 연결하는 함수와 실행 body는 framework 내부 구현이며 public contract에 포함하지 않는다.
`open` lifecycle member의 기본 구현은 아무 작업도 하지 않고 완료하며, Spot create의 기본 구현은 요청을
승인한다.

## 7. 전체 Kotlin public surface

Kotlin은 Java interface를 다시 복사하지 않는다. Java public contract를 그대로 사용할
수 있는 기능은 Java 문서를 따르고, coroutine에서 사용성이 달라지는 부분만 Kotlin
전용 interface와 extension으로 고정한다. Kotlin 전용 `suspend` 함수에는 별도
`CancellationToken`을 넣지 않는다.

### 7.1 Kotlin 전용 타입

```text
ZLinkSuspendingRequestHandler
ZLinkSuspendingSendHandler
ZLinkSuspendingPublishHandler
ZLinkSuspendingRouteRequestHandler
ZLinkSuspendingRouteSendHandler
ZLinkSuspendingSpotPacketHandler
ZLinkSuspendingSpotRequestHandler
ZLinkSuspendingSpotSubscriptionHandler
ZLinkSuspendingSpotTimerHandler
ZLinkSuspendingEntrySpotActorSendHandler
ZLinkSuspendingEntrySpotActorRequestHandler
ZLinkSuspendingSpotActorSendHandler
ZLinkSuspendingSpotActorRequestHandler
ZLinkSuspendingTypedSessionPacketHandler
ZLinkSuspendingActorFactory
ZLinkSuspendingActorTransferAdapter
ZLinkSuspendingSpot
ZLinkSuspendingEntrySpot
ZLinkSuspendingSession
```

§1~§6은 suspending handler와 lifecycle 타입의 전체 member를 고정한다. Coroutine을 Java callback에
연결하는 invoker는 framework 내부 타입이며 public inventory에 포함하지 않는다. Dispatcher와 외부 scope는
§7.2의 `useCoroutineHandlers(...)` extension으로만 설정한다.

Client Stream Connector의 Kotlin wrapper, lifecycle call과 `Flow` extension은
[Java/Kotlin Stream Connector 계약](../../../stream-connector/languages/java/03-stream-connector.ko.md)이
소유한다. Server package의 Kotlin surface에서는 해당 타입을 재선언하지 않는다.

### 7.2 Kotlin 전용 extension

다음 extension 그룹도 public contract다. overload와 receiver type은 Kotlin source의
package 구조가 아니라 이 기능 그룹을 기준으로 contract test에서 고정한다.

| 기능 | extension |
|------|-----------|
| stage 대기 | `CompletionStage<T>.await()` |
| channel/actor call | `awaitReply`, `requestToActorAwait` |
| actor directory | `findActor`, `ensureActor`, `snapshot`, `actorRef`, `awaitJoin` |
| channel send/publish | `ZLinkClient.send`, `ZLinkRouteClient.send`, `publishToTopic` |
| coroutine 구성 | `useCoroutineHandlers` |
| location store | Java `ZLinkLocationStore`와 공식 Redis 구현을 그대로 사용한다 |
| owner lease와 resolver | Java `ZLinkOwnerLeaseStore`와 resolver를 그대로 사용한다 |
| location query와 Flow | `status`, `listTopology`, `listServiceSummaries`, `locationPages`, `spots`, `actors`, `routes`, `topology`, `changes`, `Publisher.asFlow` |
| message와 dispatch | `messageOf`, `onMessageFlow` |

전체 public function 이름 inventory는 다음과 같다. 이름 목록만으로 overload를
축약하지 않으며, 바로 뒤의 정식 시그니처가 receiver, parameter와 반환형을 고정한다.

```text
actorRef
addHandler
actors
asFlow
await
awaitJoin
awaitReply
bindOrGetActor
changes
configureDispatch
configureStreamCompression
create
decode
ensureActor
findActor
getOrCreate
listLivePeers
listServiceSummaries
listTopology
locationPages
messageOf
onMessageFlow
publishToTopic
request
requestToActorAwait
resolveActorSpotHandle
resolveSpotHandle
routes
send
snapshot
spots
status
topology
useCoroutineHandlers
```

아래 코드 블록은 top-level extension의 정식 시그니처 표기다. 함수 body는 framework
구현에 속하므로 생략한다.

```kotlin
public suspend fun <T> CompletionStage<T>.await(): T

suspend fun <TReply> ZLinkRequestCall.awaitReply(
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply
suspend fun <TReply> ZLinkRequestCall.yieldReply(
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkRequestCall.yieldReply(): TReply
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
suspend fun ZLinkActorDirectory.ensureActor(
    meshName: String,
    actorId: String,
    createRequest: ZLinkMessage,
    placement: ZLinkActorPlacement = ZLinkActorPlacement.any(),
): ActorRef
suspend fun ZLinkActorDirectory.ensureActor(
    meshName: String,
    actorId: String,
    createRequest: Any,
): ActorRef
fun ActorRef.snapshot(): ActorRefSnapshot
fun ActorRefSnapshot.actorRef(): ActorRef
suspend fun ZLinkLocationReadiness.isPeerReady(
    meshName: String,
    role: ZLinkLocationRole,
    nodeRid: RoutingId? = null,
): Boolean
suspend fun ZLinkSessionActors.bindOrGetActor(actor: ActorRef): ZLinkSessionActor
suspend fun ZLinkActorJoinCall.awaitJoin(): ZLinkActorJoinResult<Void>
suspend fun <TReply> ZLinkActorJoinCall.awaitJoin(
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply>
inline suspend fun <reified TReply> ZLinkActorJoinCall.awaitJoinReply(): ZLinkActorJoinResult<TReply>
suspend fun ZLinkActorJoinCall.yieldJoin(): ZLinkActorJoinResult<Void>
suspend fun <TReply> ZLinkActorJoinCall.yieldJoin(
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply>
inline suspend fun <reified TReply> ZLinkActorJoinCall.yieldJoinReply(): ZLinkActorJoinResult<TReply>
suspend fun <T> ZLinkWorkerCall<T>.yieldWorker(): T
```

```kotlin
fun ZLinkClient.send(
    meshName: String,
    channelName: String,
    message: Message,
): ZLinkSendCall
fun ZLinkClient.request(
    meshName: String,
    channelName: String,
    message: Message,
): ZLinkRequestCall
fun ZLinkFanoutClient.publishToTopic(
    channelName: String,
    topic: String,
    message: Message,
): ZLinkPublishCall
fun ZLinkRouteClient.send(
    meshName: String,
    target: RoutingId,
    message: Message,
): ZLinkSendCall
fun ZLinkRouteClient.request(
    meshName: String,
    target: RoutingId,
    message: Message,
): ZLinkRequestCall
fun ZLinkRouteClient.send(
    target: SpotHandle,
    message: Message,
): ZLinkSendCall
fun ZLinkRouteClient.request(
    target: SpotHandle,
    message: Message,
): ZLinkRequestCall
```

```kotlin
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    meshName: String,
): ZLinkSpotCreateResult
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    meshName: String,
    request: ZLinkMessage,
): ZLinkSpotCreateResult
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    meshName: String,
    spotRid: RoutingId,
): ZLinkSpotCreateResult
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.getOrCreate(
    meshName: String,
    spotRid: RoutingId,
): ZLinkSpotCreateResult
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.getOrCreate(
    meshName: String,
    spotRid: RoutingId,
    request: ZLinkMessage,
): ZLinkSpotCreateResult

fun ZLinkFrameworkOptions.useCoroutineHandlers(dispatcher: CoroutineDispatcher)
fun ZLinkFrameworkOptions.useCoroutineHandlers(
    scope: CoroutineScope,
    dispatcher: CoroutineDispatcher,
)
fun ZLinkFrameworkOptions.configureStreamCompression(
    configure: ZLinkStreamCompressionBuilder.() -> Unit,
): ZLinkFrameworkOptions
inline fun ZLinkFrameworkOptions.configureDispatch(
    block: ZLinkDispatchOptions.() -> Unit,
): ZLinkDispatchOptions
fun ZLinkDispatchOptions.onMessageFlow(
    observer: (ZLinkMessageFlowEvent) -> Unit,
): ZLinkDispatchOptions
fun ZLinkDispatchOptions.onRuntimeError(
    sink: (ZLinkRuntimeErrorEvent) -> Unit,
): ZLinkDispatchOptions
public fun messageOf(value: Any): ZLinkMessage
public inline fun <reified T> ZLinkMessage.decode(): T
inline fun <reified THandler : Any> ZLinkSpotHandlerRegistry.addHandler()
```

```kotlin
suspend fun ZLinkPeerLocationResolver.listLivePeers(
    filter: ZLinkPeerLocationFilter,
): List<ZLinkPeerLocation>
suspend fun SpotHandleResolver.resolveSpotHandle(
    meshName: String,
    spotRid: RoutingId,
): SpotHandle?
suspend fun ActorSpotHandleResolver.resolveActorSpotHandle(
    meshName: String,
    actorId: String,
): SpotHandle?
suspend fun ZLinkLocationRuntimeQuery.status(): ZLinkLocationRuntimeStatus
suspend fun ZLinkLocationRuntimeQuery.listPeerLocations(
    filter: ZLinkPeerLocationFilter,
): List<ZLinkPeerLocation>
suspend fun ZLinkLocationRuntimeQuery.listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation>
suspend fun ZLinkLocationRuntimeQuery.listActorLocations(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation>
suspend fun ZLinkLocationRuntimeQuery.listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation>
suspend fun ZLinkLocationRuntimeQuery.listTopology(
    filter: ZLinkLocationTopologyFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkLocationTopologyEntry>
suspend fun ZLinkLocationRuntimeQuery.listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
): List<ZLinkLocationServiceSummary>
```

```kotlin
fun <T> locationPages(
    firstPage: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
    load: (ZLinkPageRequest) -> CompletionStage<ZLinkLocationPage<T>>,
): Flow<T>
fun ZLinkLocationRuntimeQuery.spots(
    filter: ZLinkSpotLocationFilter,
    pageSize: Int,
): Flow<ZLinkSpotLocation>
fun ZLinkLocationRuntimeQuery.actors(
    filter: ZLinkActorLocationFilter,
    pageSize: Int,
): Flow<ZLinkActorLocation>
fun ZLinkLocationRuntimeQuery.routes(
    filter: ZLinkRouteLocationFilter,
    pageSize: Int,
): Flow<ZLinkRouteLocation>
fun ZLinkLocationRuntimeQuery.topology(
    filter: ZLinkLocationTopologyFilter,
    pageSize: Int,
): Flow<ZLinkLocationTopologyEntry>
fun ZLinkLocationWatchStore.changes(
    filter: ZLinkLocationWatchFilter,
): Flow<ZLinkLocationChanged>
fun <T> Publisher<T>.asFlow(): Flow<T>
```

### 7.3 RouteMesh Kotlin DSL

Java의 `ZLinkFrameworkOptions`, `ZLinkMeshNodeBuilder`, `ZLinkMeshChannelBuilder`와
`ZLinkMeshPeerConnections`가 실제 builder 계약을 소유한다. Kotlin module은 그 계약을 바꾸지 않고
다음 DSL projection만 추가한다.

Actor transfer adapter를 등록하는 Kotlin application은 상속한 Java root option의
`setActorTransferTimeout(Duration)`과 `setActorTransferForwardWindow(Duration)`를 사용한다. Kotlin 전용
중복 설정 함수는 제공하지 않는다. Adapter가 하나라도 있으면 host 시작 전에 두 값을 모두 양수로
설정해야 한다.

```kotlin
fun ZLinkFrameworkOptions.routeMesh(
    meshName: String,
    configure: ZLinkMeshNodeBuilder.() -> Unit,
): ZLinkMeshNodeBuilder

fun ZLinkMeshNodeBuilder.channelName(
    channelName: String,
    configure: ZLinkMeshChannelBuilder.() -> Unit = {},
): ZLinkMeshChannelBuilder

fun ZLinkMeshPeerConnections.connect(
    expectedRoutingId: RoutingId,
    endpoint: String,
)

fun ZLinkSpotPublisherConfig.noDrop(enabled: Boolean): ZLinkSpotPublisherConfig
```

```kotlin
options.routeMesh("game") {
    listen("tcp://0.0.0.0:7300") // 이 MeshNode가 공유하는 ROUTER endpoint다.
    setRoutingId(RoutingId.from("game-1")) // 같은 MeshName에서 사용하는 identity다.
    channelName("orders") // 별도 socket 없이 논리 membership을 추가한다.
    peerConnections().connect(
        RoutingId.from("game-2"),
        "tcp://10.0.0.2:7300",
    )
    configureSpotPublisher().noDrop(true) // Logical Multicast의 기본 정책을 명시한다.
}
```

Suspending handler는 완료를 Java `CompletionStage`로 전달하고 별도 cancellation token을 받지 않는다.
MeshNode one-way call은 `trySubmit()` 또는 suspending `submit()`으로 admission 결과를 제공한다.
Request, join과 worker call의 coroutine 대기는 해당 `await()` 또는 `yield()` extension이 실행 문맥
반납과 재개를 소유한다.

## 8. 관측·운영 표면 (metrics · flow correlation · drain)

메트릭·flow correlation·drain의 사용 의미는 Java 공개 계약([Spring Boot Monitoring](../java/01-system-structure.ko.md)
§8~§10)을 그대로 따른다. 정확한 runtime/result interface는
[Java handler interface §4.2](../java/02-handler-interfaces.ko.md#42-runtime-monitoring)를 따르며,
여기서 Java 타입을 복사해 다시 정의하지 않는다. Kotlin은 같은 Java runtime 위에 **관용 델타만** 추가한다.

따라서 Kotlin application도 Java의 `ZLinkMeshNodeSnapshot`, `ZLinkLogicalMulticastSnapshot`,
`ZLinkMeshRuntimeEvent`, `ZLinkRouteMeshRuntime`과 `ZLinkMeshDrainResult`를 그대로 사용한다. Multicast
snapshot과 runtime/message-flow event의 `remoteSnapshotCount`, `remoteAdmittedCount`,
`remoteDroppedCount`, `localSnapshotCount`, `localAdmittedCount`, `localDroppedCount`도 같은 이름과 의미를
유지한다. `lifecycleGeneration`과 `descriptorRevision`은 해당 runtime event에 필요한 경우에만 존재한다.

| 대상 | Java 표면(정본) | Kotlin 델타 |
|------|------------------|-------------|
| 메트릭 | Micrometer `MeterRegistry` 자동 바인딩(무설정) | 없음 — Spring Boot Kotlin 앱도 동일 |
| flow id | 기존 message-flow 설정에 따라 자동 생성·전파 | 별도 DSL 없음. Java event의 `flowId`와 `flowOrigin`을 그대로 사용 |
| runtime error sink | Java `ZLinkRuntimeErrorSink`/`ZLinkRuntimeErrorEvent` | `onRuntimeError { ... }` 람다 adapter. `observer_failed`/`message_flow_observer`를 그대로 사용 |
| MeshNode drain 정책 | `useDrainPolicy(ZLinkMeshNodeDrainPolicy.RELEASE_AND_RECREATE)` | 동일(builder DSL) |
| drain 명시 제어 | `ZLinkRouteMeshRuntime` { `drain(meshName, deadline)`, `awaitDrained(meshName)`, `isReady(meshName)` } | 별도 extension 없음 — Java 메서드가 반환하는 `CompletionStage<ZLinkMeshDrainResult>`를 기존 `CompletionStage.await()`(§7.2)로 대기: `routeMeshRuntime.drain(meshName, deadline).await()` |
| drain 상태 관측 | `ZLinkRouteMeshRuntime.observe(meshName, capacity)`의 `ZLinkMeshRuntimeEvent` | 별도 람다 adapter 없이 Java `Flow.Publisher` 표면을 그대로 사용 |

Java `drain`이 멤버이므로 Kotlin은 이름이 겹치는 `drain` extension을 두지 않고 기존 `CompletionStage.await()`
확장을 재사용한다(표면 축소, §7.2 취소 규칙 — 별도 `CancellationToken` 없음).
`ZLinkFlowOrigin`·`ZLinkMeshNodeDrainPolicy`·`ZLinkDrainForceReason`·`ZLinkMeshDrainResult`와
`ZLinkRouteMeshRuntime` 타입은 Java 타입을 그대로 사용한다.

drain stage를 기다리는 coroutine이 취소되면 그 coroutine의 continuation과 callback registration만
정리한다. 공유 `CompletionStage<ZLinkMeshDrainResult>`에 `cancel`을 전파하지 않으며 이미 시작한 drain은
계속 실행된다. 이 규칙은 일반 request stage의 취소 전파와 구분해 contract test로 고정한다.

> §7.1 타입 목록·§7.2 함수 inventory는 이 §8의 `.await()` 재사용을 포함한 것으로 읽는다. 새 drain
> extension이나 runtime event adapter를 추가하지 않으므로 inventory는 늘어나지 않는다.
