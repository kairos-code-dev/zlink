package systems.zlink.samples.kotlin.gamequest.server.gameapi.infrastructure.zlink

import org.springframework.stereotype.Component
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressRes

@Component
class ZLinkQuestProgressSynchronizer(
    private val channels: ZLinkClient,
) : GameplayActionService.QuestProgressSynchronizer {
    override fun sync(missionName: String, playerId: String): SyncQuestProgressRes =
        channels
            .requestToChannel(
                SampleNames.questMissionChannel(missionName),
                SyncQuestProgressReq(playerId),
            )
            .timeout(SampleNames.RequestTimeout)
            .await(SyncQuestProgressRes::class.java)
}
