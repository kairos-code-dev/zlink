package systems.zlink.framework.kotlin

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorFactory
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
import kotlinx.coroutines.runBlocking

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
        cancellationToken: CancellationToken,
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
        cancellationToken: CancellationToken,
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
        cancellationToken: CancellationToken,
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
        cancellationToken: CancellationToken,
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
    final override fun create(actorId: String, context: ZLinkActorContext): ZLinkActor =
        blocking {
            createActor(actorId, context)
        }

    protected abstract suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor
}

abstract class ZLinkSuspendingSpot<TActor : ZLinkActor> : ZLinkSpot<TActor> {
    abstract override fun context(): ZLinkSpotContext

    final override fun onCreate(request: ZLinkMessage): ZLinkSpotCreateResponse =
        blocking {
            onCreateSuspending(request)
        }

    final override fun onInitialize() {
        blocking {
            onInitializeSuspending()
        }
    }

    final override fun onClosing() {
        blocking {
            onClosingSuspending()
        }
    }

    final override fun onActorJoin(
        actor: TActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        blocking {
            onActorJoinSuspending(actor, request, cancellationToken)
        }

    protected open suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse =
        ZLinkSpotCreateResponse.accept()

    protected open suspend fun onInitializeSuspending() {
    }

    protected open suspend fun onClosingSuspending() {
    }

    protected open suspend fun onActorJoinSuspending(
        actor: TActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.reject()
}

abstract class ZLinkSuspendingSession : ZLinkSession {
    abstract override fun context(): ZLinkSessionContext

    final override fun onConnected() {
        blocking {
            onConnectedSuspending()
        }
    }

    final override fun onDisconnected() {
        blocking {
            onDisconnectedSuspending()
        }
    }

    final override fun onError(error: ZLinkStreamError) {
        blocking {
            onErrorSuspending(error)
        }
    }

    final override fun onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
        blocking {
            onDispatchSuspending(dispatch, payload)
        }
    }

    protected open suspend fun onConnectedSuspending() {
    }

    protected open suspend fun onDisconnectedSuspending() {
    }

    protected open suspend fun onErrorSuspending(error: ZLinkStreamError) {
    }

    protected open suspend fun onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
    }
}

private fun <T> blocking(block: suspend () -> T): T =
    runBlocking {
        block()
    }
