package systems.zlink.samples.kotlin.gamequest.server.configuration

import java.time.Duration

/** Channel, topic, discovery, node, and packet names for the GameQuest sample. */
object SampleNames {
    const val FanoutChannel = "gamequest.events"
    const val GameplayTopic = "gameplay.event"
    const val QuestSpotDiscovery = "gamequest.quest.spot"
    const val QuestSpotNode = "gamequest.quest.node"
    const val StreamNode = "gamequest.stream"

    const val SubscribePacket = "SubscribeQuestReq"
    const val ProgressPacket = "QuestProgressNotify"
    const val CompletedPacket = "QuestCompletedNotify"

    val RequestTimeout: Duration = Duration.ofSeconds(10)

    /** Client-facing action channel hosted by a GameApi instance (`api-a`/`api-b`). */
    fun gameApiActionChannel(apiName: String): String = "gamequest.gameapi.$apiName"

    /** Channel a QuestMission instance hosts for snapshot/notify exchange with GameApi. */
    fun questMissionChannel(missionName: String): String = "gamequest.mission.$missionName"
}
