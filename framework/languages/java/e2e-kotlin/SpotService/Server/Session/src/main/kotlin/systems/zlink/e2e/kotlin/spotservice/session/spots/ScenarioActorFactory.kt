package systems.zlink.e2e.kotlin.spotservice.session.spots

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorFactory

class ScenarioActorFactory : ZLinkSuspendingActorFactory() {
    override suspend fun createActor(
        actorId: String,
        context: ZLinkActorContext
    ): ZLinkActor = ScenarioActor(actorId, context)
}
