package systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions

import kotlinx.coroutines.future.await
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.kotlin.ZLinkSuspendingSession
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.streams.ZLinkSessionActor
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkSessionDispatchContext

class CustomerSession(
    private val context: ZLinkSessionContext,
    private val handlers: ZLinkSessionPacketDispatcher<ZLinkSessionContext>,
    private val sessions: CustomerSessionDirectory,) : ZLinkSuspendingSession() {
    override fun context(): ZLinkSessionContext = context

    override suspend fun onConnectedSuspending() {
        sessions.add(context)
    }

    override suspend fun onDisconnectedSuspending() {
        sessions.remove(context)
    }

    override suspend fun onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
        if (ZLinkAwait.await(handlers.tryHandleAsync(context, dispatch, payload))) {
            return
        }
        val actor = requireSingleBoundActor(dispatch.packetName())
        ZLinkAwait.await(actor.relay(payload))
    }

    private fun requireSingleBoundActor(packetName: String): ZLinkSessionActor =
        when (context.actors().bound().size) {
            1 -> context.actors().bound()[0]
            0 -> throw IllegalStateException(
                "Client must subscribe before relaying packet '$packetName'",
            )
            else -> throw IllegalStateException(
                "Exactly one actor must be bound before relaying packet '$packetName'",
            )
        }
}
