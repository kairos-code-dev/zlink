package systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.handlers

import systems.zlink.framework.channels.ZLinkPublishContext
import systems.zlink.framework.channels.ZLinkPublishHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.CustomerSessionDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify

@ZLinkHandlerGroup("status")
class DeliveryStatusFanoutHandler(
    private val sessions: CustomerSessionDirectory,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkPublishHandler<DeliveryStatusNotify> {
    override fun handle(message: DeliveryStatusNotify, context: ZLinkPublishContext) {
        coroutines.blocking {
            sessions.publish(message)
        }
    }
}
