package systems.zlink.e2e.kotlin.spotservice.play.spots

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.handlers.*
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext

class MismatchedSpot(
    private val context: ZLinkSpotContext
) : ZLinkSpot<ZLinkActor> {
    override fun context(): ZLinkSpotContext = context

    override fun configure() {
    }
}
