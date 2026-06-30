package systems.zlink.e2e.kotlin.spotservice.session.spots

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorFactory

class ScenarioActorFactory : ZLinkActorFactory {
    override fun create(
        actorId: String,
        context: ZLinkActorContext
    ): ZLinkActor = ScenarioActor(actorId, context)
}
