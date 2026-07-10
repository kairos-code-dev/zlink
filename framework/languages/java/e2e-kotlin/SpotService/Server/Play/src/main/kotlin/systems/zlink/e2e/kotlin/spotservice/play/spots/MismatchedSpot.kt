package systems.zlink.e2e.kotlin.spotservice.play.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.handlers.*
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext

class MismatchedSpot(
    private val context: ZLinkSpotContext
) : ZLinkSpot<ZLinkActor> {
    override fun onJoinedActor(actor: ZLinkActor, cancellationToken: CancellationToken) {
    }

    override fun onLeaveActor(actor: ZLinkActor, cancellationToken: CancellationToken) {
    }
    override fun context(): ZLinkSpotContext = context

    override fun configure() {
    }
}
