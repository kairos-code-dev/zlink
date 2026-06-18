package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory

class CustomerActorFactory() : ZLinkSuspendingActorFactory() {
    override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor =
        CustomerActor(actorId, context)
}
