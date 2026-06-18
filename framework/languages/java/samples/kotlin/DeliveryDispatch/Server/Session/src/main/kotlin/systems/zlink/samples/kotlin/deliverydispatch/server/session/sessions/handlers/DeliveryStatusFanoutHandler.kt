package systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.handlers

import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.kotlin.ZLinkSuspendingPublishHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.CustomerSessionDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify

@ZLinkHandlerGroup("status")
class DeliveryStatusFanoutHandler(
    private val sessions: CustomerSessionDirectory,
) : ZLinkSuspendingPublishHandler<DeliveryStatusNotify> {
    override suspend fun handle(message: DeliveryStatusNotify, context: ZLinkPublishContext) = run {
        sessions.publish(message)
    }
}
