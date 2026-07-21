package systems.zlink.framework.kotlin

import java.util.concurrent.CompletionStage
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.future.future
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorFactory
import systems.zlink.framework.actors.ZLinkActorTransferAdapter
import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.channels.ZLinkRouteSendContext
import systems.zlink.framework.channels.ZLinkSendContext
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamError
import systems.zlink.framework.streams.ZLinkSessionDispatchContext

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

    suspend fun handle(spot: TSpot, message: TMessage, context: ZLinkSendContext) =
        handle(spot, message)
}

interface ZLinkSuspendingSpotRequestHandler<TSpot : Any, TRequest, TReply> {
    suspend fun handle(spot: TSpot, request: TRequest): TReply

    suspend fun handle(
        spot: TSpot,
        request: TRequest,
        context: ZLinkRequestContext,
    ): TReply = handle(spot, request)
}

interface ZLinkSuspendingSpotSubscriptionHandler<TSpot : Any, TEvent> {
    suspend fun handle(spot: TSpot, event: TEvent)

    suspend fun handle(
        spot: TSpot,
        event: TEvent,
        context: ZLinkPublishContext,
    ) = handle(spot, event)
}

interface ZLinkSuspendingSpotTimerHandler<TSpot : ZLinkSpot<*>> {
    suspend fun handle(spot: TSpot, tick: ZLinkTimerTick)
}

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

interface ZLinkSuspendingTypedSessionPacketHandler<
    TSessionContext : ZLinkSessionContext,
    TMessage : Any,
> {
    fun packetName(): String

    fun messageType(): Class<TMessage>

    suspend fun handle(context: TSessionContext, dispatch: ZLinkSessionDispatchContext, message: TMessage)
}

abstract class ZLinkSuspendingActorFactory : ZLinkActorFactory {
    final override fun create(actorId: String, context: ZLinkActorContext): CompletionStage<ZLinkActor> =
        coroutineStage {
            createActor(actorId, context)
        }

    protected abstract suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor
}

abstract class ZLinkSuspendingActorTransferAdapter<TActor : ZLinkActor> :
    ZLinkActorTransferAdapter<TActor> {
    final override fun transferOut(
        actor: TActor,
    ): CompletionStage<ZLinkMessage> = coroutineStage {
        transferOutSuspending(actor)
    }

    final override fun transferIn(
        actorId: String,
        context: ZLinkActorContext,
        state: ZLinkMessage,
    ): CompletionStage<TActor> = coroutineStage {
        transferInSuspending(actorId, context, state)
    }

    protected abstract suspend fun transferOutSuspending(
        actor: TActor,
    ): ZLinkMessage

    protected abstract suspend fun transferInSuspending(
        actorId: String,
        context: ZLinkActorContext,
        state: ZLinkMessage,
    ): TActor
}

abstract class ZLinkSuspendingSpot<TActor : ZLinkActor> : ZLinkSpot<TActor> {
    abstract override fun context(): ZLinkSpotContext

    final override fun onCreate(request: ZLinkMessage): CompletionStage<ZLinkSpotCreateResponse> =
        coroutineStage { onCreateSuspending(request) }

    final override fun onInitialize(): CompletionStage<Void> =
        coroutineVoidStage { onInitializeSuspending() }

    final override fun onClosing(): CompletionStage<Void> =
        coroutineVoidStage { onClosingSuspending() }

    final override fun onActorJoin(actorId: String, request: ZLinkMessage): CompletionStage<ZLinkSpotActorJoinResponse> =
        coroutineStage { onActorJoinSuspending(actorId, request) }

    final override fun onJoinedActor(actor: TActor): CompletionStage<Void> =
        coroutineVoidStage { onJoinedActorSuspending(actor) }

    final override fun onLeaveActor(actor: TActor): CompletionStage<Void> =
        coroutineVoidStage { onLeaveActorSuspending(actor) }

    final override fun onDisconnectActor(actor: TActor): CompletionStage<Void> =
        coroutineVoidStage { onDisconnectActorSuspending(actor) }

    protected open suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse =
        ZLinkSpotCreateResponse.accept()

    protected open suspend fun onInitializeSuspending() {
    }

    protected open suspend fun onClosingSuspending() {
    }

    protected abstract suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse

    protected abstract suspend fun onJoinedActorSuspending(actor: TActor)

    protected abstract suspend fun onLeaveActorSuspending(actor: TActor)

    protected open suspend fun onDisconnectActorSuspending(actor: TActor) {
    }
}

abstract class ZLinkSuspendingEntrySpot<TActor : ZLinkActor> : ZLinkEntrySpot<TActor> {
    abstract override fun context(): systems.zlink.framework.spots.ZLinkEntrySpotContext

    final override fun onInitialize(): CompletionStage<Void> = coroutineVoidStage { onInitializeSuspending() }

    final override fun onClosing(): CompletionStage<Void> = coroutineVoidStage { onClosingSuspending() }

    final override fun onCreateActor(
        actor: TActor,
        createRequest: ZLinkMessage,
    ): CompletionStage<Void> = coroutineVoidStage { onCreateActorSuspending(actor, createRequest) }

    final override fun onActorJoin(
        actorId: String,
        request: ZLinkMessage,
    ): CompletionStage<ZLinkSpotActorJoinResponse> = coroutineStage { onActorJoinSuspending(actorId, request) }

    final override fun onJoinedActor(actor: TActor): CompletionStage<Void> =
        coroutineVoidStage { onJoinedActorSuspending(actor) }

    final override fun onLeaveActor(actor: TActor): CompletionStage<Void> =
        coroutineVoidStage { onLeaveActorSuspending(actor) }

    final override fun onDisconnectActor(actor: TActor): CompletionStage<Void> =
        coroutineVoidStage { onDisconnectActorSuspending(actor) }

    protected open suspend fun onInitializeSuspending() {
    }

    protected open suspend fun onClosingSuspending() {
    }

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

    protected open suspend fun onDisconnectActorSuspending(actor: TActor) {
    }
}

abstract class ZLinkSuspendingSession : ZLinkSession {
    abstract override fun context(): ZLinkSessionContext

    final override fun onConnected(): CompletionStage<Void> = coroutineVoidStage { onConnectedSuspending() }

    final override fun onDisconnected(): CompletionStage<Void> = coroutineVoidStage { onDisconnectedSuspending() }

    final override fun onError(error: ZLinkStreamError): CompletionStage<Void> =
        coroutineVoidStage { onErrorSuspending(error) }

    final override fun onDispatch(
        dispatch: ZLinkSessionDispatchContext,
        payload: ZLinkMessage,
    ): CompletionStage<Void> = coroutineVoidStage { onDispatchSuspending(dispatch, payload) }

    protected open suspend fun onConnectedSuspending() {
    }

    protected open suspend fun onDisconnectedSuspending() {
    }

    protected open suspend fun onErrorSuspending(error: ZLinkStreamError) {
    }

    protected open suspend fun onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
    }
}

private val bridgeScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

private fun <T> coroutineStage(block: suspend () -> T): CompletionStage<T> =
    bridgeScope.future(ZLinkCoroutineInvocationContext.capture(Dispatchers.Default)) { block() }

private fun coroutineVoidStage(block: suspend () -> Unit): CompletionStage<Void> =
    bridgeScope.future(ZLinkCoroutineInvocationContext.capture(Dispatchers.Default)) {
        block()
        null
    }
