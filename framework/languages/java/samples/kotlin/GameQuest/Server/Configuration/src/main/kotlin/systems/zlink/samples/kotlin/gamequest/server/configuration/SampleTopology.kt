package systems.zlink.samples.kotlin.gamequest.server.configuration

import systems.zlink.contracts.core.RoutingId

/**
 * Endpoint topology and `PlayerId` owner routing for GameQuest.
 *
 * Like the other samples the run script reserves free TCP ports and overrides
 * the defaults below through `-Dzlink.samples.gamequest.*` system properties.
 * The .NET sample exposes the GameApi action surface over HTTP; the Kotlin
 * sample mirrors the same scenario over ZLink client/server channels, so the
 * `*HttpBaseUrl` endpoints become channel endpoints.
 */
object SampleTopology {
    val RegistryPubEndpoint = property("registryPubEndpoint", "tcp://127.0.0.1:47590")
    val RegistryRouterEndpoint = property("registryRouterEndpoint", "tcp://127.0.0.1:47591")

    val FanoutPublisherAEndpoint = property("fanoutPublisherAEndpoint", "tcp://127.0.0.1:47592")
    val FanoutPublisherBEndpoint = property("fanoutPublisherBEndpoint", "tcp://127.0.0.1:47593")

    val GameApiAActionEndpoint = property("gameApiAActionEndpoint", "tcp://127.0.0.1:47594")
    val GameApiBActionEndpoint = property("gameApiBActionEndpoint", "tcp://127.0.0.1:47595")

    val GameApiAStreamEndpoint = property("gameApiAStreamEndpoint", "tcp://127.0.0.1:47596")
    val GameApiBStreamEndpoint = property("gameApiBStreamEndpoint", "tcp://127.0.0.1:47597")

    val MissionAActionEndpoint = property("missionAActionEndpoint", "tcp://127.0.0.1:47598")
    val MissionBActionEndpoint = property("missionBActionEndpoint", "tcp://127.0.0.1:47599")

    val MissionASpotEndpoint = property("missionASpotEndpoint", "tcp://127.0.0.1:47600")
    val MissionASpotRouterEndpoint = property("missionASpotRouterEndpoint", "tcp://127.0.0.1:47601")
    val MissionBSpotEndpoint = property("missionBSpotEndpoint", "tcp://127.0.0.1:47602")
    val MissionBSpotRouterEndpoint = property("missionBSpotRouterEndpoint", "tcp://127.0.0.1:47603")

    val MissionASpotRid: RoutingId = RoutingId.from("7101")
    val MissionBSpotRid: RoutingId = RoutingId.from("7102")

    val StoreDirectory = property("storeDirectory", System.getProperty("java.io.tmpdir") + "/gamequest")

    fun gameApiActionEndpoint(apiName: String): String =
        if (apiName == "api-b") GameApiBActionEndpoint else GameApiAActionEndpoint

    fun gameApiStreamEndpoint(apiName: String): String =
        if (apiName == "api-b") GameApiBStreamEndpoint else GameApiAStreamEndpoint

    fun fanoutPublisherEndpointForApi(apiName: String): String =
        if (apiName == "api-b") FanoutPublisherBEndpoint else FanoutPublisherAEndpoint

    fun forQuestMission(missionName: String): QuestMissionInstanceTopology =
        if (missionName == "mission-b") {
            QuestMissionInstanceTopology(
                "mission-b", MissionBSpotEndpoint, MissionBSpotRouterEndpoint, MissionBSpotRid, 1,
            )
        } else {
            QuestMissionInstanceTopology(
                "mission-a", MissionASpotEndpoint, MissionASpotRouterEndpoint, MissionASpotRid, 0,
            )
        }

    fun missionActionEndpoint(missionName: String): String =
        if (missionName == "mission-b") MissionBActionEndpoint else MissionAActionEndpoint

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.gamequest.$name", defaultValue)
}
