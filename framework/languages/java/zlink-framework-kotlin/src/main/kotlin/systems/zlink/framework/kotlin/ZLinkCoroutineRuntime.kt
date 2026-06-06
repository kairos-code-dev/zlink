package systems.zlink.framework.kotlin

import java.util.concurrent.CompletionStage
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.future.future
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
import systems.zlink.framework.spots.ZLinkEntrySpotActorDisconnectedHandler
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.framework.spots.ZLinkSpotActorDisconnectedHandler
import systems.zlink.framework.spots.ZLinkSpotActorJoinHandler
import systems.zlink.framework.spots.ZLinkSpotActorLeftHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.framework.spots.ZLinkSpotActorSendHandler
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotPostActorJoinedHandler
import systems.zlink.framework.spots.ZLinkSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketHandler
import systems.zlink.framework.streams.ZLinkStreamError as FrameworkStreamError
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.stream.connector.ZLinkStreamConnectionState
import systems.zlink.stream.connector.ZLinkStreamConnectionStateHandler
import systems.zlink.stream.connector.ZLinkStreamDisconnectedHandler
import systems.zlink.stream.connector.ZLinkStreamError
import systems.zlink.stream.connector.ZLinkStreamErrorHandler
import systems.zlink.stream.connector.ZLinkStreamMessage
import systems.zlink.stream.connector.ZLinkStreamMessageHandler

class ZLinkCoroutineRuntime @JvmOverloads constructor(
    private val dispatcher: CoroutineDispatcher = Dispatchers.Default,
) : AutoCloseable {
    private val job = SupervisorJob()
    private val scope = CoroutineScope(job + dispatcher)

    fun <TRequest, TReply> requestHandler(
        block: suspend (TRequest, ZLinkRequestContext) -> TReply,
    ): ZLinkRequestHandler<TRequest, TReply> =
        ZLinkRequestHandler { request, context ->
            stage {
                block(request, context)
            }
        }

    fun <TMessage> sendHandler(
        block: suspend (TMessage, ZLinkSendContext) -> Unit,
    ): ZLinkSendHandler<TMessage> =
        ZLinkSendHandler { message, context ->
            voidStage {
                block(message, context)
            }
        }

    fun <TMessage> publishHandler(
        block: suspend (TMessage, ZLinkPublishContext) -> Unit,
    ): ZLinkPublishHandler<TMessage> =
        ZLinkPublishHandler { message, context ->
            voidStage {
                block(message, context)
            }
        }

    fun <TRequest, TReply> routeRequestHandler(
        block: suspend (TRequest, ZLinkRouteRequestContext) -> TReply,
    ): ZLinkRouteRequestHandler<TRequest, TReply> =
        ZLinkRouteRequestHandler { request, context ->
            stage {
                block(request, context)
            }
        }

    fun <TMessage> routeSendHandler(
        block: suspend (TMessage, ZLinkRouteSendContext) -> Unit,
    ): ZLinkRouteSendHandler<TMessage> =
        ZLinkRouteSendHandler { message, context ->
            voidStage {
                block(message, context)
            }
        }

    fun <TSpot : ZLinkSpot> spotTimerHandler(
        block: suspend (TSpot, ZLinkTimerTick) -> Unit,
    ): ZLinkSpotTimerHandler<TSpot> =
        ZLinkSpotTimerHandler { spot, tick ->
            voidStage {
                block(spot, tick)
            }
        }

    fun actorFactory(
        block: suspend (String, ZLinkActorContext) -> ZLinkActor,
    ): ZLinkActorFactory =
        ZLinkActorFactory { actorId, context ->
            stage {
                block(actorId, context)
            }
        }

    fun <TSessionContext : ZLinkSessionContext> sessionPacketHandler(
        packetName: String,
        block: suspend (TSessionContext, ZLinkStreamHeader, Message) -> Unit,
    ): ZLinkSessionPacketHandler<TSessionContext> =
        object : ZLinkSessionPacketHandler<TSessionContext> {
            override fun packetName(): String = packetName

            override fun handleAsync(
                context: TSessionContext,
                header: ZLinkStreamHeader,
                payload: Message,
            ): CompletionStage<Void> =
                voidStage {
                    block(context, header, payload)
                }
        }

    fun <TEvent : ZLinkRuntimeEvent> runtimeEventHandler(
        block: suspend (TEvent) -> Unit,
    ): ZLinkRuntimeEventHandler<TEvent> =
        ZLinkRuntimeEventHandler { event ->
            voidStage {
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
        scope.cancel()
    }

    fun <T> completionStage(block: suspend CoroutineScope.() -> T): CompletionStage<T> =
        stage(block)

    fun voidCompletionStage(block: suspend CoroutineScope.() -> Unit): CompletionStage<Void> =
        voidStage(block)

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
    final override fun createAsync(
        actorId: String,
        context: ZLinkActorContext,
    ): CompletionStage<ZLinkActor> =
        coroutines.completionStage {
            create(actorId, context)
        }

    protected abstract suspend fun create(
        actorId: String,
        context: ZLinkActorContext,
    ): ZLinkActor
}

abstract class ZLinkCoroutineSpotTimerHandler<TSpot : ZLinkSpot>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotTimerHandler<TSpot> {
    final override fun handleAsync(
        spot: TSpot,
        tick: ZLinkTimerTick,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            handle(spot, tick)
        }

    protected abstract suspend fun handle(spot: TSpot, tick: ZLinkTimerTick)
}

abstract class ZLinkCoroutineEntrySpotActorSendHandler<
    TEntrySpot : ZLinkEntrySpot,
    TActor : ZLinkActor,
    TMessage>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage> {
    final override fun handleAsync(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            handle(entrySpot, actor, context, message, cancellationToken)
        }

    protected abstract suspend fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
        cancellationToken: CancellationToken,
    )
}

abstract class ZLinkCoroutineEntrySpotActorRequestHandler<
    TEntrySpot : ZLinkEntrySpot,
    TActor : ZLinkActor,
    TRequest,
    TReply>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply> {
    final override fun handleAsync(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): CompletionStage<TReply> =
        coroutines.completionStage {
            handle(entrySpot, actor, context, request, cancellationToken)
        }

    protected abstract suspend fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): TReply
}

abstract class ZLinkCoroutineSpotActorJoinHandler<
    TSpot : ZLinkSpot,
    TActor : ZLinkActor,
    TRequest,
    TReply>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotActorJoinHandler<TSpot, TActor, TRequest, TReply> {
    final override fun handleAsync(
        spot: TSpot,
        actor: TActor,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): CompletionStage<TReply> =
        coroutines.completionStage {
            handle(spot, actor, request, cancellationToken)
        }

    protected abstract suspend fun handle(
        spot: TSpot,
        actor: TActor,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): TReply
}

abstract class ZLinkCoroutineSpotActorRequestHandler<
    TSpot : ZLinkSpot,
    TActor : ZLinkActor,
    TRequest,
    TReply>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply> {
    final override fun handleAsync(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): CompletionStage<TReply> =
        coroutines.completionStage {
            handle(spot, actor, context, request, cancellationToken)
        }

    protected abstract suspend fun handle(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorRequestContext,
        request: TRequest,
        cancellationToken: CancellationToken,
    ): TReply
}

abstract class ZLinkCoroutineSpotActorSendHandler<
    TSpot : ZLinkSpot,
    TActor : ZLinkActor,
    TMessage>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotActorSendHandler<TSpot, TActor, TMessage> {
    final override fun handleAsync(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            handle(spot, actor, context, message, cancellationToken)
        }

    protected abstract suspend fun handle(
        spot: TSpot,
        actor: TActor,
        context: ZLinkSpotActorSendContext,
        message: TMessage,
        cancellationToken: CancellationToken,
    )
}

abstract class ZLinkCoroutineSpotPostActorJoinedHandler<
    TSpot,
    TActor : ZLinkActor>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotPostActorJoinedHandler<TSpot, TActor> {
    final override fun handleAsync(
        spot: TSpot,
        actor: TActor,
        result: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            handle(spot, actor, result, cancellationToken)
        }

    protected abstract suspend fun handle(
        spot: TSpot,
        actor: TActor,
        result: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    )
}

abstract class ZLinkCoroutineSpotActorLeftHandler<
    TSpot,
    TActor : ZLinkActor>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotActorLeftHandler<TSpot, TActor> {
    final override fun handleAsync(
        spot: TSpot,
        actor: TActor,
        result: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            handle(spot, actor, result, cancellationToken)
        }

    protected abstract suspend fun handle(
        spot: TSpot,
        actor: TActor,
        result: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    )
}

abstract class ZLinkCoroutineEntrySpotActorDisconnectedHandler<
    TEntrySpot : ZLinkEntrySpot,
    TActor : ZLinkActor>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpotActorDisconnectedHandler<TEntrySpot, TActor> {
    final override fun handleAsync(
        entrySpot: TEntrySpot,
        actor: TActor,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            handle(entrySpot, actor, cancellationToken)
        }

    protected abstract suspend fun handle(
        entrySpot: TEntrySpot,
        actor: TActor,
        cancellationToken: CancellationToken,
    )
}

abstract class ZLinkCoroutineSpotActorDisconnectedHandler<
    TSpot,
    TActor : ZLinkActor>(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpotActorDisconnectedHandler<TSpot, TActor> {
    final override fun handleAsync(
        spot: TSpot,
        actor: TActor,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            handle(spot, actor, cancellationToken)
        }

    protected abstract suspend fun handle(
        spot: TSpot,
        actor: TActor,
        cancellationToken: CancellationToken,
    )
}

abstract class ZLinkCoroutineSessionPacketHandler<TSessionContext : ZLinkSessionContext>(
    private val coroutines: ZLinkCoroutineRuntime,
    private val packetName: String,
) : ZLinkSessionPacketHandler<TSessionContext> {
    final override fun packetName(): String = packetName

    final override fun handleAsync(
        context: TSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            handle(context, header, payload)
        }

    protected abstract suspend fun handle(
        context: TSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    )
}

abstract class ZLinkCoroutineSpot(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSpot {
    abstract override fun context(): ZLinkSpotContext

    final override fun onCreateAsync(createParts: MutableList<Message>): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            onCreate(createParts)
        }

    final override fun onInitializeAsync(): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            onInitialize()
        }

    final override fun onClosingAsync(): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            onClosing()
        }

    protected open suspend fun onCreate(createParts: MutableList<Message>) {
    }

    protected open suspend fun onInitialize() {
    }

    protected open suspend fun onClosing() {
    }
}

abstract class ZLinkCoroutineSession(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkSession {
    abstract override fun context(): ZLinkSessionContext

    final override fun onConnectedAsync(): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            onConnected()
        }

    final override fun onDisconnectedAsync(): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            onDisconnected()
        }

    final override fun onErrorAsync(error: FrameworkStreamError): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            onError(error)
        }

    final override fun onDispatchAsync(
        header: ZLinkStreamHeader,
        payload: Message,
    ): CompletionStage<Void> =
        coroutines.voidCompletionStage {
            onDispatch(header, payload)
        }

    protected open suspend fun onConnected() {
    }

    protected open suspend fun onDisconnected() {
    }

    protected open suspend fun onError(error: FrameworkStreamError) {
    }

    protected open suspend fun onDispatch(header: ZLinkStreamHeader, payload: Message) {
    }
}
