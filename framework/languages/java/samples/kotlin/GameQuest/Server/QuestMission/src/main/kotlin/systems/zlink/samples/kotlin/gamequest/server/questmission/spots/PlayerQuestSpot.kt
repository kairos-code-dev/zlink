package systems.zlink.samples.kotlin.gamequest.server.questmission.spots

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse

/**
 * Owner aggregate per player, routed by `playerId`. Mirrors the .NET
 * `PlayerQuestSpot`: the QuestMission instance that owns the player hosts exactly
 * one spot per player.
 */
class PlayerQuestSpot(
    private val context: ZLinkSpotContext,
    private val json: ObjectMapper,
) : ZLinkSpot<ZLinkActor> {
    var playerId: String = ""
        private set

    override fun context(): ZLinkSpotContext = context

    override fun onCreate(request: Message): ZLinkSpotCreateResponse {
        playerId = decode(request).playerId
        System.err.printf(
            "gamequest player quest spot ready player=%s spot=%s%n",
            playerId, context.spotRid(),
        )
        return ZLinkSpotCreateResponse.accept()
    }

    private fun decode(request: Message): PlayerQuestSpotCreateReq {
        val bytes = request.toByteArray()
        if (bytes.isEmpty()) {
            return PlayerQuestSpotCreateReq("")
        }
        return json.readValue(bytes, PlayerQuestSpotCreateReq::class.java)
    }

    data class PlayerQuestSpotCreateReq(val playerId: String)
}
