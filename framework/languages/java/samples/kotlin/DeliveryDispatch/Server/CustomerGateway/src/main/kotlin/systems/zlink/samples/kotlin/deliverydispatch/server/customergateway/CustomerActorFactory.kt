package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorFactory

class CustomerActorFactory : ZLinkActorFactory {
    override fun create(actorId: String, context: ZLinkActorContext): ZLinkActor =
        CustomerActor(actorId, context)
}
