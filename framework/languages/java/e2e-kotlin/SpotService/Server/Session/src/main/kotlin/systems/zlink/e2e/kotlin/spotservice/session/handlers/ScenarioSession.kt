package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkStreamError

class ScenarioSession(
    private val context: ZLinkSessionContext,
    private val handlers: ZLinkSessionPacketDispatcher<ZLinkSessionContext>,
    private val evidence: ScenarioState
) : ZLinkSession {
    override fun context(): ZLinkSessionContext = context

    override fun onConnected() {
        evidence.record("StreamConnected", "session", context.sessionId())
    }

    override fun onDisconnected() {
        context.actors().bound().take(1).forEach { actor ->
            actor.notifyDisconnected().toCompletableFuture().join()
            evidence.record("ActorDisconnectNotified", "session", actor.actorId())
        }
        evidence.record("StreamDisconnected", "session", context.sessionId())
    }

    override fun onError(error: ZLinkStreamError) {
        evidence.record("StreamError", "session", error.error().name)
    }

    override fun onDispatch(
        dispatch: ZLinkSessionDispatchContext,
        payload: ZLinkMessage
    ) {
        evidence.record("StreamInbound", "session", dispatch.packetName())
        val handled = handlers.tryHandleAsync(context, dispatch, payload)
            .toCompletableFuture()
            .join()
        if (handled) {
            return
        }
        requireActor(dispatch).relay(payload).toCompletableFuture().join()
    }

    private fun requireActor(dispatch: ZLinkSessionDispatchContext): ZLinkSessionActor {
        val actorId = dispatch.metadata()["actor-id"]
        if (actorId != null && actorId.isNotBlank()) {
            return context.actors().find(actorId)
                .orElseThrow { IllegalStateException("actor is not bound: $actorId") }
        }
        return when (context.actors().bound().size) {
            1 -> context.actors().bound()[0]
            0 -> throw IllegalStateException("ActorAuthRequest is required before actor packet")
            else -> throw IllegalStateException("actor-id metadata is required for multiple bound actors")
        }
    }
}
