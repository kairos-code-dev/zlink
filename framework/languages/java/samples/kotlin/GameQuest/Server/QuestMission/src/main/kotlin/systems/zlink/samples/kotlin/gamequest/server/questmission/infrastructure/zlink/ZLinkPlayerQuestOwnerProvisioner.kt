package systems.zlink.samples.kotlin.gamequest.server.questmission.infrastructure.zlink

import com.fasterxml.jackson.databind.ObjectMapper
import java.nio.charset.StandardCharsets
import org.springframework.stereotype.Component
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.gamequest.server.questmission.application.QuestEventProcessor
import systems.zlink.samples.kotlin.gamequest.server.questmission.spots.PlayerQuestSpot

@Component
class ZLinkPlayerQuestOwnerProvisioner(
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) : QuestEventProcessor.PlayerQuestOwnerProvisioner {
    override fun ensure(playerId: String) {
        ZLinkAwait.await(
            spots.getOrCreate(
                PlayerQuestSpot::class.java,
                RoutingId.from("player:$playerId".toByteArray(StandardCharsets.UTF_8)),
                PlayerQuestSpot.PlayerQuestSpotCreateReq(playerId),
            ),
        )
    }
}
