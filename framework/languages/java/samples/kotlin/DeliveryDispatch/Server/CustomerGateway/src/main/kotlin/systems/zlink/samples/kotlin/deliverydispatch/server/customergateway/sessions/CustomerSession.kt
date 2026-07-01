package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.sessions

import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.streams.ZLinkSession
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher
import systems.zlink.framework.streams.ZLinkStreamError

class CustomerSession(
    private val sessionContext: ZLinkSessionContext,
    private val handlers: ZLinkSessionPacketDispatcher<ZLinkSessionContext>,
) : ZLinkSession {
    override fun context(): ZLinkSessionContext = sessionContext

    override fun onConnected() {
    }

    override fun onDisconnected() {
        sessionContext.actors().bound()
            .forEach { actor -> ZLinkAwait.await(actor.notifyDisconnected()) }
    }

    override fun onError(error: ZLinkStreamError) {
    }

    override fun onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
        val handled = ZLinkAwait.await(handlers.tryHandleAsync(sessionContext, dispatch, payload))
        if (handled) {
            return
        }
        val actor = when (sessionContext.actors().bound().size) {
            1 -> sessionContext.actors().bound()[0]
            0 -> error("Client must subscribe before relaying packet '${dispatch.packetName()}'")
            else -> error("Exactly one customer actor must be bound before relaying packet '${dispatch.packetName()}'")
        }
        ZLinkAwait.await(actor.relay(payload))
    }
}
