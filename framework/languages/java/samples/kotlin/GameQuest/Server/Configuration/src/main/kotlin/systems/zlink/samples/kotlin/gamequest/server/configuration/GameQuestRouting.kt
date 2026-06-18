package systems.zlink.samples.kotlin.gamequest.server.configuration

import java.nio.charset.StandardCharsets

/** Player owner routing: a QuestMission instance owns a player by `playerId` hash. */
object GameQuestRouting {
    fun ownerIndex(playerId: String): Int {
        var sum = 0
        for (value in playerId.toByteArray(StandardCharsets.UTF_8)) {
            sum += value.toInt() and 0xFF
        }
        return sum % 2
    }
}
