package systems.zlink.samples.kotlin.gamequest.server.questmission.application

import org.springframework.stereotype.Component
import systems.zlink.samples.kotlin.gamequest.server.configuration.GameQuestRouting
import systems.zlink.samples.kotlin.gamequest.server.configuration.QuestMissionInstanceTopology

/** Decides whether this QuestMission instance owns a given player. Mirrors the .NET `QuestOwnerRouter`. */
@Component
class QuestOwnerRouter(
    instance: QuestMissionInstanceTopology,
) {
    private val ownerIndex = instance.ownerIndex

    fun isLocalOwner(playerId: String): Boolean =
        GameQuestRouting.ownerIndex(playerId) == ownerIndex
}
