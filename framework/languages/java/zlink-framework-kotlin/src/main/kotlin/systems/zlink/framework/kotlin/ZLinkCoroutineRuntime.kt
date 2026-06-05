package systems.zlink.framework.kotlin

import java.util.concurrent.CompletionStage
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.future.future
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
import systems.zlink.framework.spots.ZLinkSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
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
