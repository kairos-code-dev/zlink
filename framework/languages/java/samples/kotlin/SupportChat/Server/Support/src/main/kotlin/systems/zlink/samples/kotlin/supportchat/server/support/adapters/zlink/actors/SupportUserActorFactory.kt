package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory

class SupportUserActorFactory() : ZLinkSuspendingActorFactory() {
    override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor =
        SupportUserActor(actorId, context)
}
