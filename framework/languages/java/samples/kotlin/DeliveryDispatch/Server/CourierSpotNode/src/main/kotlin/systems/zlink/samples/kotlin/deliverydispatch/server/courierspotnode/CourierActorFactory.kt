package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory

class CourierActorFactory : ZLinkSuspendingActorFactory() {
    override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor =
        CourierActor(actorId, context)
}
