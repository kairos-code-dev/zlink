package systems.zlink.samples.kotlin.gamequest.server.questmission.infrastructure.zlink

import org.springframework.stereotype.Component
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.questmission.application.QuestEventProcessor
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotRes

@Component
class ZLinkGameApiSnapshotClient(
    private val channels: ZLinkClient,
) : QuestEventProcessor.GameApiSnapshotClient {
    override fun read(apiName: String, playerId: String): GetGameplaySnapshotRes =
        channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(apiName),
                GetGameplaySnapshotReq(playerId),
            )
            .timeout(SampleNames.RequestTimeout)
            .await(GetGameplaySnapshotRes::class.java)
}
