package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory

class PlayerActorFactory() : ZLinkSuspendingActorFactory() {
    override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor =
        PlayerActor(actorId, context)
}
