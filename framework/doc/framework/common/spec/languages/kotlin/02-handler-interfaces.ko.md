# ZLink Framework Kotlin Handler Contract

이 문서는 `zlink-framework-kotlin`이 추가하는 suspending handler와 lifecycle adapter의
정식 시그니처를 고정한다. 사용법은 Kotlin guide에서 설명하며, Kotlin 구현과 contract
test는 이 문서의 시그니처를 따라야 한다.
본문 선언은 정식 목표 계약이고, §7.3은 현재 Kotlin adapter와의 차이를 기록하는
비규범 구현 ledger다.

Java interface를 그대로 사용하는 표면은
[Java interface 계약](../java/02-handler-interfaces.ko.md)을 따른다. 아래 Kotlin 타입의
package는 `systems.zlink.framework.kotlin`이다.

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

interface ZLinkSuspendingSpotRequestHandler<TSpot : ZLinkSpot<*>, TRequest, TReply> {
    suspend fun handle(spot: TSpot, request: TRequest): TReply
}

interface ZLinkSuspendingSpotSubscriptionHandler<TSpot : ZLinkSpot<*>, TEvent> {
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
```

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
Java base interface를 구현하는 `final override`는 `CompletionStage`를 반환하고 내부에서
`CoroutineScope.future { ... }` 또는 동등한 non-blocking bridge로 `suspend` member를
실행한다. `runBlocking`으로 반환형을 맞추지 않는다.

```kotlin
abstract class ZLinkSuspendingActorFactory : ZLinkActorFactory {
    final override fun create(
        actorId: String,
        context: ZLinkActorContext,
    ): CompletionStage<ZLinkActor> = frameworkCoroutineBridge {
        createActor(actorId, context)
    }

    protected abstract suspend fun createActor(
        actorId: String,
        context: ZLinkActorContext,
    ): ZLinkActor
}

abstract class ZLinkSuspendingActorTransferAdapter<TActor : ZLinkActor> :
    ZLinkActorTransferAdapter<TActor> {
    final override fun transferOut(actor: TActor): CompletionStage<ZLinkMessage> =
        frameworkCoroutineBridge { transferOutSuspending(actor) }

    final override fun transferIn(
        actorId: String,
        context: ZLinkActorContext,
        state: ZLinkMessage,
    ): CompletionStage<TActor> = frameworkCoroutineBridge {
        transferInSuspending(actorId, context, state)
    }

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

    final override fun onCreate(
        request: ZLinkMessage,
    ): CompletionStage<ZLinkSpotCreateResponse> =
        frameworkCoroutineBridge { onCreateSuspending(request) }
    final override fun onInitialize(): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onInitializeSuspending() }
    final override fun onClosing(): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onClosingSuspending() }

    final override fun onActorJoin(
        actorId: String,
        request: ZLinkMessage,
    ): CompletionStage<ZLinkSpotActorJoinResponse> = frameworkCoroutineBridge {
        onActorJoinSuspending(actorId, request)
    }

    final override fun onJoinedActor(actor: TActor): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onJoinedActorSuspending(actor) }

    final override fun onLeaveActor(actor: TActor): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onLeaveActorSuspending(actor) }

    final override fun onDisconnectActor(actor: TActor): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onDisconnectActorSuspending(actor) }

    protected abstract suspend fun onCreateSuspending(
        request: ZLinkMessage,
    ): ZLinkSpotCreateResponse

    protected open suspend fun onInitializeSuspending() = Unit
    protected open suspend fun onClosingSuspending() = Unit

    protected abstract suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse

    protected abstract suspend fun onJoinedActorSuspending(actor: TActor)

    protected abstract suspend fun onLeaveActorSuspending(actor: TActor)
    protected open suspend fun onDisconnectActorSuspending(actor: TActor) = Unit
}

abstract class ZLinkSuspendingEntrySpot<TActor : ZLinkActor> :
    ZLinkEntrySpot<TActor> {
    abstract override fun context(): ZLinkEntrySpotContext

    final override fun onInitialize(): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onInitializeSuspending() }
    final override fun onClosing(): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onClosingSuspending() }

    final override fun onCreateActor(
        actor: TActor,
        createRequest: ZLinkMessage,
    ): CompletionStage<Void> = frameworkVoidCoroutineBridge {
        onCreateActorSuspending(actor, createRequest)
    }

    final override fun onActorJoin(
        actorId: String,
        request: ZLinkMessage,
    ): CompletionStage<ZLinkSpotActorJoinResponse> = frameworkCoroutineBridge {
        onActorJoinSuspending(actorId, request)
    }

    final override fun onJoinedActor(actor: TActor): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onJoinedActorSuspending(actor) }

    final override fun onLeaveActor(actor: TActor): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onLeaveActorSuspending(actor) }

    final override fun onDisconnectActor(actor: TActor): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onDisconnectActorSuspending(actor) }

    protected open suspend fun onInitializeSuspending() = Unit
    protected open suspend fun onClosingSuspending() = Unit

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
    protected open suspend fun onDisconnectActorSuspending(actor: TActor) = Unit
}
```

## 6. Session lifecycle adapter

```kotlin
abstract class ZLinkSuspendingSession : ZLinkSession {
    abstract override fun context(): ZLinkSessionContext

    final override fun onConnected(): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onConnectedSuspending() }
    final override fun onDisconnected(): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onDisconnectedSuspending() }
    final override fun onError(error: ZLinkStreamError): CompletionStage<Void> =
        frameworkVoidCoroutineBridge { onErrorSuspending(error) }
    final override fun onDispatch(
        dispatch: ZLinkSessionDispatchContext,
        payload: ZLinkMessage,
    ): CompletionStage<Void> = frameworkVoidCoroutineBridge {
        onDispatchSuspending(dispatch, payload)
    }

    protected open suspend fun onConnectedSuspending() = Unit
    protected open suspend fun onDisconnectedSuspending() = Unit
    protected open suspend fun onErrorSuspending(error: ZLinkStreamError) = Unit
    protected open suspend fun onDispatchSuspending(
        dispatch: ZLinkSessionDispatchContext,
        payload: ZLinkMessage,
    ) = Unit
}
```

`frameworkCoroutineBridge`와 Unit 결과를 `CompletionStage<Void>`로 바꾸는
`frameworkVoidCoroutineBridge`는 framework 내부 구현이며 public API가 아니다. 위 본문은
각 Java callback이 대응하는 suspending member를 비차단 방식으로 호출한다는 계약을
명확히 보이기 위해 함께 적었다.

## 7. 전체 Kotlin public surface와 구현 차이

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
ZLinkCoroutineSuspendHandlerInvoker
ZLinkKotlinLifecycleCall
ZLinkKotlinSendCall
ZLinkKotlinStreamConnector
ZLinkStreamTypedWaitCall
ZLinkSuspendingLocationStore
```

§1~§6은 suspending handler와 lifecycle 타입의 전체 member를 고정한다. 나머지 public
타입의 목표 선언은 다음과 같다.

```kotlin
class ZLinkCoroutineSuspendHandlerInvoker : ZLinkSuspendHandlerInvoker {
    constructor(dispatcher: CoroutineDispatcher = Dispatchers.Default)
    constructor(
        scope: CoroutineScope,
        dispatcher: CoroutineDispatcher = Dispatchers.Default,
    )

    override fun supports(method: Method): Boolean
    override fun invoke(
        handler: Any,
        method: Method,
        logicalArguments: Array<Any>,
    ): CompletionStage<Any>
}

class ZLinkKotlinStreamConnector(
    internal val inner: ZLinkStreamConnector,
) {
    val isConnected: Boolean
    val state: ZLinkStreamConnectionState
    val options: ZLinkStreamConnectorOptions
    val pendingDispatchCount: Int
    fun receivedCount(name: String): Int
    fun observeInbound(observer: ZLinkStreamInboundObserver): AutoCloseable
    fun on(
        name: String,
        handler: ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload>,
    ): AutoCloseable
    inline fun <reified TPayload> on(
        handler: ZLinkStreamMessageHandler<TPayload>,
    ): AutoCloseable
    fun onErrorReceived(handler: ZLinkStreamErrorHandler): AutoCloseable
    fun onDisconnected(handler: ZLinkStreamDisconnectedHandler): AutoCloseable
    fun onConnectionStateChanged(
        handler: ZLinkStreamConnectionStateHandler,
    ): AutoCloseable
    fun connect(): ZLinkKotlinLifecycleCall
    fun disconnect(): ZLinkKotlinLifecycleCall
    fun reconnect(): ZLinkKotlinLifecycleCall
    fun close(): ZLinkKotlinLifecycleCall
    fun dispatch(): ZLinkKotlinLifecycleCall
    fun send(payload: ZLinkStreamEncodedPayload): ZLinkKotlinSendCall
    fun send(payload: Any): ZLinkKotlinSendCall
    fun request(payload: ZLinkStreamEncodedPayload): ZLinkStreamRequestCall
    fun request(payload: Any): ZLinkTypedStreamRequestCall
    inline fun <reified TPayload> waitFor(): ZLinkStreamTypedWaitCall<TPayload>
    inline fun <reified TPayload> waitFor(
        name: String,
    ): ZLinkStreamTypedWaitCall<TPayload>
    fun messages(
        packetName: String,
    ): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>
    fun errors(): Flow<ZLinkStreamError>
}

class ZLinkKotlinLifecycleCall {
    suspend fun await()
}

class ZLinkKotlinSendCall {
    fun submit()
}

class ZLinkStreamTypedWaitCall<TPayload> {
    fun timeout(timeout: Duration): ZLinkStreamTypedWaitCall<TPayload>
    fun where(
        predicate: (ZLinkStreamMessage<TPayload>) -> Boolean,
    ): ZLinkStreamTypedWaitCall<TPayload>
    suspend fun await(): ZLinkStreamMessage<TPayload>
}
```

`ZLinkKotlinLifecycleCall.await()`은 coroutine을 중단한 뒤 Java stage 완료로 재개한다.
`ZLinkKotlinSendCall.submit()`은 one-way 전송 완료 객체를 만들지 않는다.
raw send의 packet identity는 `ZLinkStreamEncodedPayload`에 이미 포함되어 있고 typed
send의 identity는 payload type descriptor가 정하므로 Kotlin call wrapper도 이름
override를 제공하지 않는다.

```kotlin
abstract class ZLinkSuspendingLocationStore : ZLinkLocationStore {
    protected abstract suspend fun updatePeer(
        peer: ZLinkPeerLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removePeer(
        key: ZLinkPeerLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun listPeerLocations(
        filter: ZLinkPeerLocationFilter,
    ): List<ZLinkPeerLocation>
    protected abstract suspend fun updateSpot(
        spot: ZLinkSpotLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removeSpot(
        key: ZLinkSpotLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun resolveSpot(
        key: ZLinkSpotLocationKey,
    ): ZLinkSpotLocation?
    protected abstract suspend fun listSpotLocations(
        filter: ZLinkSpotLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkSpotLocation>
    protected abstract suspend fun updateActor(
        actor: ZLinkActorLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removeActor(
        key: ZLinkActorLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun resolveActor(
        key: ZLinkActorLocationKey,
    ): ZLinkActorLocation?
    protected abstract suspend fun listActorLocations(
        filter: ZLinkActorLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkActorLocation>
    protected abstract suspend fun updateRoute(
        route: ZLinkRouteLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removeRoute(
        key: ZLinkRouteLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun resolveRoute(
        key: ZLinkRouteLocationKey,
    ): ZLinkRouteLocation?
    protected abstract suspend fun listRouteLocations(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkRouteLocation>
    protected abstract suspend fun renewOwnerLease(
        ownerId: String,
        nodeRid: RoutingId,
        leaseTtl: Duration,
    ): ZLinkOwnerLeaseRenewal
    protected abstract suspend fun removeOwnerLease(ownerId: String): Boolean
    protected abstract suspend fun removeAllByOwner(ownerId: String): Long
    protected abstract suspend fun listOwnerLeases(): ZLinkOwnerLeaseSnapshot
}
```

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
| location store | `updatePeer`, `removePeer`, `listPeerLocations`, `updateSpot`, `removeSpot`, `resolveSpot`, `listSpotLocations`, `updateActor`, `removeActor`, `resolveActor`, `listActorLocations`, `updateRoute`, `removeRoute`, `resolveRoute`, `listRouteLocations` |
| owner lease와 resolver | `renewOwnerLease`, `removeOwnerLease`, `removeAllByOwner`, `awaitOwnerLeases`, `listLivePeers`, `resolveSpotHandle`, `resolveActorSpotHandle`, `isPeerReady` |
| location query와 Flow | `status`, `listTopology`, `listServiceSummaries`, `locationPages`, `spots`, `actors`, `routes`, `topology`, `changes`, `Publisher.asFlow` |
| stream connector | `kotlin`, compression 설정 extension, request `await`, `messages`, `errors` |
| message와 dispatch | `messageOf`, `onMessageFlow` |

전체 public function 이름 inventory는 다음과 같다. 이름 목록만으로 overload를
축약하지 않으며, 바로 뒤의 목표 시그니처가 receiver, parameter와 반환형을 고정한다.

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
configureStreamCompression
configureDispatch
create
decode
ensureActor
errors
findActor
getOrCreate
isPeerReady
kotlin
listActorLocations
listLivePeers
awaitOwnerLeases
listPeerLocations
listRouteLocations
listServiceSummaries
listSpotLocations
listTopology
locationPages
messageOf
messages
onMessageFlow
publishToTopic
request
removeActor
removeAllByOwner
removeOwnerLease
removePeer
removeRoute
removeSpot
renewOwnerLease
requestToActorAwait
resolveActor
resolveActorSpotHandle
resolveRoute
resolveSpot
resolveSpotHandle
routes
send
snapshot
spots
status
topology
updateActor
updatePeer
updateRoute
updateSpot
useCoroutineHandlers
waitFor
withDefaultStreamCompression
withLz4StreamCompression
withStreamCompression
withoutStreamCompression
```

아래 코드 블록은 top-level extension의 정식 시그니처 표기다. 함수 body는 framework
구현에 속하므로 생략한다.

```kotlin
public suspend fun <T> CompletionStage<T>.await(): T

suspend fun <TReply> ZLinkRequestCall.awaitReply(
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkRequestCall.awaitReply(): TReply
suspend fun <TReply> ZLinkActorRequestCall.awaitReply(
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkActorRequestCall.awaitReply(): TReply
suspend fun <TReply> ZLinkActorClient.requestToActorAwait(
    actorRef: ActorRef,
    request: Any,
    replyType: Class<TReply>,
): TReply
inline suspend fun <reified TReply> ZLinkActorClient.requestToActorAwait(
    actorRef: ActorRef,
    request: Any,
): TReply
suspend fun ZLinkActorDirectory.findActor(actorId: String): ActorRef?
suspend fun ZLinkActorDirectory.ensureActor(
    actorId: String,
    createRequest: ZLinkMessage,
    placement: ZLinkActorPlacement = ZLinkActorPlacement.any(),
): ActorRef
suspend fun ZLinkActorDirectory.ensureActor(
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
```

```kotlin
fun ZLinkClient.send(channelName: String, message: Message)
inline suspend fun <reified TReply> ZLinkClient.request(
    channelName: String,
    message: Message,
): TReply
fun ZLinkFanoutClient.publishToTopic(
    channelName: String,
    topic: String,
    message: Message,
)
fun ZLinkRouteClient.send(
    channelName: String,
    target: RoutingId,
    message: Message,
)
inline suspend fun <reified TReply> ZLinkRouteClient.request(
    channelName: String,
    target: RoutingId,
    message: Message,
): TReply
fun ZLinkRouteClient.send(
    target: SpotHandle,
    message: Message,
)
inline suspend fun <reified TReply> ZLinkRouteClient.request(
    target: SpotHandle,
    message: Message,
): TReply
```

```kotlin
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(): ZLinkSpotCreateResult
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    request: ZLinkMessage,
): ZLinkSpotCreateResult
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.create(
    spotRid: RoutingId,
): ZLinkSpotCreateResult
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.getOrCreate(
    spotRid: RoutingId,
): ZLinkSpotCreateResult
inline suspend fun <reified TSpot : ZLinkSpot<*>> ZLinkSpotManager.getOrCreate(
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
public fun messageOf(value: Any): ZLinkMessage
public inline fun <reified T> ZLinkMessage.decode(): T
inline fun <reified THandler : Any> ZLinkSpotHandlerRegistry.addHandler()
```

```kotlin
suspend fun ZLinkPeerLocationStore.updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkPeerLocationStore.removePeer(
    key: ZLinkPeerLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult
suspend fun ZLinkPeerLocationStore.listPeerLocations(
    filter: ZLinkPeerLocationFilter,
): List<ZLinkPeerLocation>
suspend fun ZLinkSpotLocationStore.updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkSpotLocationStore.removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult
suspend fun ZLinkSpotLocationStore.resolveSpot(
    key: ZLinkSpotLocationKey,
): ZLinkSpotLocation?
suspend fun ZLinkSpotLocationStore.listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation>
suspend fun ZLinkActorLocationStore.updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkActorLocationStore.removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult
suspend fun ZLinkActorLocationStore.resolveActor(
    key: ZLinkActorLocationKey,
): ZLinkActorLocation?
suspend fun ZLinkActorLocationStore.listActorLocations(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation>
suspend fun ZLinkRouteLocationStore.updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkRouteLocationStore.removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult
suspend fun ZLinkRouteLocationStore.resolveRoute(
    key: ZLinkRouteLocationKey,
): ZLinkRouteLocation?
suspend fun ZLinkRouteLocationStore.listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation>
```

```kotlin
suspend fun ZLinkLocationStore.renewOwnerLease(
    ownerId: String,
    nodeRid: RoutingId,
    leaseTtl: Duration,
): ZLinkOwnerLeaseRenewal
suspend fun ZLinkLocationStore.removeOwnerLease(ownerId: String): Boolean
suspend fun ZLinkLocationStore.removeAllByOwner(ownerId: String): Long
suspend fun ZLinkLocationStore.awaitOwnerLeases(): ZLinkOwnerLeaseSnapshot
suspend fun ZLinkPeerLocationResolver.listLivePeers(
    filter: ZLinkPeerLocationFilter,
): List<ZLinkPeerLocation>
suspend fun SpotHandleResolver.resolveSpotHandle(
    spotRid: RoutingId,
): SpotHandle?
suspend fun ActorSpotHandleResolver.resolveActorSpotHandle(actorId: String): SpotHandle?
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

```kotlin
fun ZLinkStreamConnector.kotlin(): ZLinkKotlinStreamConnector
fun ZLinkStreamConnectorOptions.withDefaultStreamCompression(): ZLinkStreamConnectorOptions
fun ZLinkStreamConnectorOptions.withLz4StreamCompression(): ZLinkStreamConnectorOptions
fun ZLinkStreamConnectorOptions.withStreamCompression(
    codec: ZLinkStreamCompressionCodec,
): ZLinkStreamConnectorOptions
fun ZLinkStreamConnectorOptions.withoutStreamCompression(): ZLinkStreamConnectorOptions
suspend fun ZLinkStreamRequestCall.await(): ZLinkStreamEncodedPayload
inline suspend fun <reified TReply> ZLinkStreamRequestCall.awaitReply(): TReply
inline suspend fun <reified TReply> ZLinkTypedStreamRequestCall.awaitReply(): TReply
inline suspend fun <reified TPayload> ZLinkStreamWaitCall.await(): ZLinkStreamMessage<TPayload>
inline fun <reified TPayload> ZLinkStreamConnector.waitFor(): ZLinkStreamTypedWaitCall<TPayload>
fun ZLinkStreamConnector.messages(
    packetName: String,
): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>
fun ZLinkStreamConnector.errors(): Flow<ZLinkStreamError>
```

### 7.3 목표 interface와 현재 구현

| 대상 symbol | 목표 Kotlin 계약 | 현재 public 선언/adapter | 판정과 구현 작업 |
|-------------|--------------------|--------------------------|------------------|
| `ZLinkSuspendingRequestHandler`, `ZLinkSuspendingSendHandler`, `ZLinkSuspendingPublishHandler`, 두 route handler | `suspend fun handle` 완료를 Java stage로 전달 | coroutine completion을 `CompletionStage`로 반환하는 adapter 사용 | 일치 — Kotlin unit test가 incomplete stage와 예외 완료를 검증한다. |
| 네 Spot actor suspending handler와 네 Spot packet/request/subscription/timer handler | `suspend fun handle`, 별도 token 없음 | token 없이 coroutine completion을 stage로 반환 | 일치 — handler 등록과 dispatch test가 suspending 완료를 검증한다. |
| `ZLinkSuspendingActorFactory.create` | `suspend` 완료를 Java `CompletionStage<TActor>`로 전달 | coroutine completion을 `CompletionStage<ZLinkActor>`로 반환 | 일치 |
| `ZLinkSuspendingSpot`, `ZLinkSuspendingEntrySpot` lifecycle | `suspend`, 별도 token 없음 | lifecycle override가 token 없이 `CompletionStage`를 반환 | 일치 |
| `ZLinkSuspendingActorTransferAdapter.transferOut/transferIn` | `suspend`, 별도 token 없음 | transfer coroutine completion을 stage로 전달 | 일치 |
| `CompletionStage<T>.await()`와 내부 `awaitFrameworkStage` | coroutine을 중단하고 stage callback으로 재개 | `suspendCancellableCoroutine` callback bridge 사용 | 일치 — waiter 취소가 framework stage를 취소하지 않고 완료 오류를 unwrap하는 integration test가 통과한다. |
| `ZLinkSuspendingTypedSessionPacketHandler.handle` | typed payload를 직접 받고 suspend 완료 | typed registry가 suspending handler를 직접 호출하고 stage 완료를 기다림 | 일치 |
| `ZLinkKotlinLifecycleCall.await`, stream request/wait `await` | `kotlinx-coroutines-jdk8` 방식의 non-blocking stage 대기 | connector wrapper는 `kotlinx.coroutines.future.await` 사용 | 일치 — Java core의 stage가 실제 비동기 완료를 나타내는지 contract test로 고정한다. |
| `ZLinkKotlinSendCall.submit` | one-way, 완료 객체 없음 | `inner.submit()` 결과를 반환하지 않음 | 일치 |
| actor one-way completion extension | 목표 public 계약에 없음 | `awaitSend`와 `sendToActorAwait`를 제거하고 Java one-way `submit()`을 직접 사용 | 일치 |
| `ZLinkFanoutClient.publishToTopic` | 일반 `fun`, one-way 완료 객체 없음 | 일반 함수가 내부 `submit()`만 호출 | 일치 |
| request/join/worker yield extension | 별도 extension 없음. 일반 `await`가 실행 문맥을 보존 | yield extension 없음 | 일치 |
| Spot messaging target | `SpotHandle` extension과 handle resolver | `resolveSpotHandle`과 `resolveActorSpotHandle`이 nullable handle을 반환 | 일치 |
| 이 절의 top-level extension 선언 | receiver, overload, parameter, default와 반환형을 위 시그니처로 고정 | type·function 이름과 overload 수를 검사하고, 각 public method의 JVM descriptor snapshot을 검증함 | 일치 — Kotlin 이름과 JVM에서 충돌을 피하려고 바꾼 이름까지 contract test가 고정한다. |

## 8. 관측·운영 표면 (metrics · flow correlation · drain)

메트릭·flow correlation·drain의 계약은 Java 공개 계약([Spring Boot Monitoring](../java/01-system-structure.ko.md)
§8~§10)을 그대로 따르고, 여기서 Java 타입을 복사해 다시 정의하지 않는다. Kotlin은 같은 Java runtime
위에 **관용 델타만** 추가한다.

| 대상 | Java 표면(정본) | Kotlin 델타 |
|------|------------------|-------------|
| 메트릭 | Micrometer `MeterRegistry` 자동 바인딩(무설정) | 없음 — Spring Boot Kotlin 앱도 동일 |
| flow id | 기존 message-flow 설정에 따라 자동 생성·전파 | 별도 DSL 없음. Java event의 `flowId`와 `flowOrigin`을 그대로 사용 |
| SPOT drain 정책 | `useDrainPolicy(ZLinkSpotDrainPolicy.RELEASE_AND_RECREATE)` | 동일(builder DSL) |
| drain 명시 제어 | `ZLinkDrainControl` { `drain(Duration)`, `awaitDrained()`, `isReady()` } | 별도 extension 없음 — Java 메서드가 반환하는 `CompletionStage<ZLinkDrainResult>`를 기존 `CompletionStage.await()`(§7.2)로 대기: `drainControl.drain(deadline).await()` |
| drain 상태 관측 | `ZLinkRuntimeEventHandler<ZLinkDrainEvent>` | `onDrain { event -> ... }` 람다 옵저버(에르고노믹스) |

Java `drain`이 멤버이므로 Kotlin은 이름이 겹치는 `drain` extension을 두지 않고 기존 `CompletionStage.await()`
확장을 재사용한다(표면 축소, §7.2 취소 규칙 — 별도 `CancellationToken` 없음).
`ZLinkFlowOrigin`·`ZLinkSpotDrainPolicy`·`ZLinkDrainEvent`·`ZLinkDrainResult` 타입은 Java 타입을
그대로 사용한다.

drain stage를 기다리는 coroutine이 취소되면 그 coroutine의 continuation과 callback registration만
정리한다. 공유 `CompletionStage<ZLinkDrainResult>`에 `cancel`을 전파하지 않으며 이미 시작한 drain은
계속 실행된다. 이 규칙은 일반 request stage의 취소 전파와 구분해 contract test로 고정한다.

> §7.1 타입 목록·§7.2 함수 inventory는 이 §8의 `onDrain` 람다와 위 `.await()` 재사용을 포함한 것으로
> 읽는다(새 `drain` extension은 추가하지 않으므로 inventory 증가 없음).
