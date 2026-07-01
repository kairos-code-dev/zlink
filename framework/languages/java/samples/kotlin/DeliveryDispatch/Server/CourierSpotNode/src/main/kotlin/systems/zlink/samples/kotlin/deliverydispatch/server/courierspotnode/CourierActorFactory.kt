package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorFactory

class CourierActorFactory : ZLinkActorFactory {
    override fun create(actorId: String, context: ZLinkActorContext): ZLinkActor =
        CourierActor(actorId, context)
}
