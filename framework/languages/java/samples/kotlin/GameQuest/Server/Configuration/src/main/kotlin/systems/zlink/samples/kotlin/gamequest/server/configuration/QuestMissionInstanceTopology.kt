package systems.zlink.samples.kotlin.gamequest.server.configuration

import systems.zlink.contracts.core.RoutingId

/** Resolved endpoints and owner index for a single QuestMission instance. */
data class QuestMissionInstanceTopology(
    val missionName: String,
    val spotEndpoint: String,
    val spotRouterEndpoint: String,
    val spotRid: RoutingId,
    val ownerIndex: Int,
)
