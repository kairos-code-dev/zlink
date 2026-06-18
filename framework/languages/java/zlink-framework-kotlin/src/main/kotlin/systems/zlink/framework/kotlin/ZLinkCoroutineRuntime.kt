package systems.zlink.framework.kotlin

import java.util.concurrent.CompletionStage
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.future.await
import kotlinx.coroutines.future.future
import kotlinx.coroutines.runBlocking
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorFactory
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.channels.ZLinkPublishHandler
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.channels.ZLinkRouteRequestHandler
import systems.zlink.framework.channels.ZLinkRouteSendContext
import systems.zlink.framework.channels.ZLinkRouteSendHandler
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.channels.ZLinkSendHandler
import systems.zlink.framework.monitoring.ZLinkRuntimeEvent
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.framework.spots.ZLinkSpotActorSendHandler
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamError as FrameworkStreamError
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler
import systems.zlink.stream.connector.ZLinkStreamConnectionState
import systems.zlink.stream.connector.ZLinkStreamConnectionStateHandler
import systems.zlink.stream.connector.ZLinkStreamDisconnectedHandler
import systems.zlink.stream.connector.ZLinkStreamError
import systems.zlink.stream.connector.ZLinkStreamErrorHandler
import systems.zlink.stream.connector.ZLinkStreamMessage
import systems.zlink.stream.connector.ZLinkStreamMessageHandler

suspend fun ZLinkEntrySpotContext.destroyActor(actor: ZLinkActor) {
    destroyActor(actor).await()
}

class ZLinkCoroutineRuntime : AutoCloseable {
    private val dispatcher: CoroutineDispatcher
    private val scope: CoroutineScope
    private val ownsScope: Boolean

    @JvmOverloads
    constructor(dispatcher: CoroutineDispatcher = Dispatchers.Default) {
        this.dispatcher = dispatcher
        this.scope = CoroutineScope(SupervisorJob() + dispatcher)
        this.ownsScope = true
    }

    @JvmOverloads
    constructor(
        scope: CoroutineScope,
        dispatcher: CoroutineDispatcher = Dispatchers.Default,
    ) {
        this.dispatcher = dispatcher
        this.scope = scope
        this.ownsScope = false
    }

    fun <TRequest, TReply> requestHandler(
        block: suspend (TRequest, ZLinkRequestContext) -> TReply,
    ): ZLinkRequestHandler<TRequest, TReply> =
        ZLinkRequestHandler { request, context ->
            blocking {
                block(request, context)
            }
        }

    fun <TMessage> sendHandler(
        block: suspend (TMessage, ZLinkSendContext) -> Unit,
    ): ZLinkSendHandler<TMessage> =
        ZLinkSendHandler { message, context ->
            blocking {
                block(message, context)
            }
        }

    fun <TMessage> publishHandler(
        block: suspend (TMessage, ZLinkPublishContext) -> Unit,
    ): ZLinkPublishHandler<TMessage> =
        ZLinkPublishHandler { message, context ->
            blocking {
                block(message, context)
            }
        }

    fun <TRequest, TReply> routeRequestHandler(
        block: suspend (TRequest, ZLinkRouteRequestContext) -> TReply,
    ): ZLinkRouteRequestHandler<TRequest, TReply> =
        ZLinkRouteRequestHandler { request, context ->
            blocking {
                block(request, context)
            }
        }

    fun <TMessage> routeSendHandler(
        block: suspend (TMessage, ZLinkRouteSendContext) -> Unit,
    ): ZLinkRouteSendHandler<TMessage> =
        ZLinkRouteSendHandler { message, context ->
            blocking {
                block(message, context)
            }
        }

    fun <TSpot : ZLinkSpot<*>> spotTimerHandler(
        block: suspend (TSpot, ZLinkTimerTick) -> Unit,
    ): ZLinkSpotTimerHandler<TSpot> =
        ZLinkSpotTimerHandler { spot, tick ->
            blocking {
                block(spot, tick)
            }
        }

    fun actorFactory(
        block: suspend (String, ZLinkActorContext) -> ZLinkActor,
    ): ZLinkActorFactory =
        ZLinkActorFactory { actorId, context ->
            blocking {
                block(actorId, context)
            }
        }

    fun <TSessionContext : ZLinkSessionContext> sessionPacketHandler(
        packetName: String,
        block: suspend (TSessionContext, ZLinkStreamHeader, Message) -> Unit,
    ): ZLinkSessionPacketHandler<TSessionContext> =
        object : ZLinkSessionPacketHandler<TSessionContext> {
            override fun packetName(): String = packetName

            override fun handle(
                context: TSessionContext,
                header: ZLinkStreamHeader,
                payload: Message,
            ) {
                blocking {
                    block(context, header, payload)
                }
            }
        }

    fun <TEvent : ZLinkRuntimeEvent> runtimeEventHandler(
        block: suspend (TEvent) -> Unit,
    ): ZLinkRuntimeEventHandler<TEvent> =
        ZLinkRuntimeEventHandler { event ->
            blocking {
                block(event)
            }
        }

    fun streamDisconnectedHandler(
        block: suspend () -> Unit,
    ): ZLinkStreamDisconnectedHandler =
        ZLinkStreamDisconnectedHandler {
            voidStage {
                block()
            }
        }

    fun streamConnectionStateHandler(
        block: suspend (ZLinkStreamConnectionState) -> Unit,
    ): ZLinkStreamConnectionStateHandler =
        ZLinkStreamConnectionStateHandler { state ->
            voidStage {
                block(state)
            }
        }

    fun streamErrorHandler(
        block: suspend (ZLinkStreamError) -> Unit,
    ): ZLinkStreamErrorHandler =
        ZLinkStreamErrorHandler { error ->
            voidStage {
                block(error)
            }
        }

    fun <TPayload> streamMessageHandler(
        block: suspend (ZLinkStreamMessage<TPayload>) -> Unit,
    ): ZLinkStreamMessageHandler<TPayload> =
        ZLinkStreamMessageHandler { message ->
            voidStage {
                block(message)
            }
        }

    override fun close() {
        if (ownsScope) {
            scope.cancel()
        }
    }

    fun <T> completionStage(block: suspend CoroutineScope.() -> T): CompletionStage<T> =
        stage(block)

    fun voidCompletionStage(block: suspend CoroutineScope.() -> Unit): CompletionStage<Void> =
        voidStage(block)

    fun <T> blocking(block: suspend CoroutineScope.() -> T): T =
        runBlocking(scope.coroutineContext + dispatcher) {
            block()
        }

    private fun <T> stage(block: suspend CoroutineScope.() -> T): CompletionStage<T> =
        scope.future(dispatcher) {
            block()
        }

    @Suppress("UNCHECKED_CAST")
    private fun voidStage(block: suspend CoroutineScope.() -> Unit): CompletionStage<Void> =
        scope.future(dispatcher) {
            block()
            null
        } as CompletionStage<Void>
}

abstract class ZLinkCoroutineActorFactory(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkActorFactory {
    final override fun create(
        actorId: String,
        context: ZLinkActorContext,
    ): ZLinkActor =
        coroutines.blocking {
            createActor(actorId, context)
        }

    protected abstract suspend fun createActor(
        actorId: String,
        context: ZLinkActorContext,
    ): ZLinkActor
}

abstract class ZLinkCoroutineSpotTimerHandler<TSpot : ZLinkSpot<*>>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotTimerHandler<TSpot> {
    final override fun handle(
        spot: TSpot,
        tick: ZLinkTimerTick,
    ) {
        coroutines.blocking {
            handleSuspending(spot, tick)
        }
    }

    protected abstract suspend fun handleSuspending(spot: TSpot, tick: ZLinkTimerTick)
}

abstract class ZLinkCoroutineEntrySpotActorSendHandler<
    TEntrySpot : ZLinkEntrySpot<*>,
    TActor : ZLinkActor,
    TMessage>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage> {
    final override fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
        cancellationToken: CancellationToken,
    ) {
        coroutines.blocking {
            handleSuspending(entrySpot, actor, context, message, cancellationToken)
        }
    }

    protected abstract suspend fun handleSuspending(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
        cancellationToken: CancellationToken,
    )
}

abstract class ZLinkCoroutineEntrySpotActorRequestHandler<
    TEntrySpot : ZLinkEntrySpot<*>,
    TActor : ZLinkActor,
    TRequest,
    TReply>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply> {
    final override fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): TReply =
        coroutines.blocking {
            handleSuspending(entrySpot, actor, context, request, cancellationToken)
        }

    protected abstract suspend fun handleSuspending(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): TReply
}

abstract class ZLinkCoroutineSpotActorRequestHandler<
    TSpot : ZLinkSpot<*>,
    TActor : ZLinkActor,
    TRequest,
    TReply>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply> {
    final override fun handle(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): TReply =
        coroutines.blocking {
            handleSuspending(spot, actor, context, request, cancellationToken)
        }

    protected abstract suspend fun handleSuspending(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): TReply
}

abstract class ZLinkCoroutineSpotActorSendHandler<
    TSpot : ZLinkSpot<*>,
    TActor : ZLinkActor,
    TMessage>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotActorSendHandler<TSpot, TActor, TMessage> {
    final override fun handle(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
        cancellationToken: CancellationToken,
    ) {
        coroutines.blocking {
            handleSuspending(spot, actor, context, message, cancellationToken)
        }
    }

    protected abstract suspend fun handleSuspending(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
        cancellationToken: CancellationToken,
    )
}

abstract class ZLinkCoroutineSessionPacketHandler<TSessionContext : ZLinkSessionContext>(
    private val coroutines: ZLinkCoroutineRuntime,
    private val packetName: String,
) : ZLinkSessionPacketHandler<TSessionContext> {
    final override fun packetName(): String = packetName

    final override fun handle(
        context: TSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ) {
        coroutines.blocking {
            handleSuspending(context, header, payload)
        }
    }

    protected abstract suspend fun handleSuspending(
        context: TSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    )
}

abstract class ZLinkCoroutineTypedSessionPacketHandler<TSessionContext : ZLinkSessionContext, TMessage : Any>(
    private val coroutines: ZLinkCoroutineRuntime,
    private val packetName: String,
    private val messageType: Class<TMessage>,
) : ZLinkTypedSessionPacketHandler<TSessionContext, TMessage> {
    final override fun packetName(): String = packetName

    final override fun messageType(): Class<TMessage> = messageType

    final override fun handle(
        context: TSessionContext,
        header: ZLinkStreamHeader,
        message: TMessage,
    ) {
        coroutines.blocking {
            handleSuspending(context, header, message)
        }
    }

    protected abstract suspend fun handleSuspending(
        context: TSessionContext,
        header: ZLinkStreamHeader,
        message: TMessage,
    )
}

abstract class ZLinkCoroutineSpot<TActor : ZLinkActor>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpot<TActor> {
    abstract override fun context(): ZLinkSpotContext

    final override fun onCreate(request: Message): ZLinkSpotCreateResponse =
        coroutines.blocking {
            onCreateSuspending(request)
        }

    final override fun onInitialize() {
        coroutines.blocking {
            onInitializeSuspending()
        }
    }

    final override fun onClosing() {
        coroutines.blocking {
            onClosingSuspending()
        }
    }

    final override fun onActorJoin(
        actor: TActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        coroutines.blocking {
            onActorJoinSuspending(actor, request, cancellationToken)
        }

    open override fun onJoinedActor(
        actor: TActor,
        cancellationToken: CancellationToken,
    ) {
    }

    open override fun onLeaveActor(
        actor: TActor,
        cancellationToken: CancellationToken,
    ) {
    }

    open override fun onDisconnectActor(
        actor: TActor,
        cancellationToken: CancellationToken,
    ) {
    }

    protected open suspend fun onCreateSuspending(request: Message): ZLinkSpotCreateResponse {
        return ZLinkSpotCreateResponse.accept()
    }

    protected open suspend fun onInitializeSuspending() {
    }

    protected open suspend fun onClosingSuspending() {
    }

    protected open suspend fun onActorJoinSuspending(
        actor: TActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        return ZLinkSpotActorJoinResponse.reject()
    }

}

abstract class ZLinkCoroutineSession(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSession {
    abstract override fun context(): ZLinkSessionContext

    final override fun onConnected() {
        coroutines.blocking {
            onConnectedSuspending()
        }
    }

    final override fun onDisconnected() {
        coroutines.blocking {
            onDisconnectedSuspending()
        }
    }

    final override fun onError(error: FrameworkStreamError) {
        coroutines.blocking {
            onErrorSuspending(error)
        }
    }

    final override fun onDispatch(
        header: ZLinkStreamHeader,
        payload: Message,
    ) {
        coroutines.blocking {
            onDispatchSuspending(header, payload)
        }
    }

    protected open suspend fun onConnectedSuspending() {
    }

    protected open suspend fun onDisconnectedSuspending() {
    }

    protected open suspend fun onErrorSuspending(error: FrameworkStreamError) {
    }

    protected open suspend fun onDispatchSuspending(header: ZLinkStreamHeader, payload: Message) {
    }
}
