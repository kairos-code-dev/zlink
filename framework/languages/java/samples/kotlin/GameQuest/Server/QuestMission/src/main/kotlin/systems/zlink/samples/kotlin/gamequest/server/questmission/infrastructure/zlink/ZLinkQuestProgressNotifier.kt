package systems.zlink.samples.kotlin.gamequest.server.questmission.infrastructure.zlink

import org.springframework.stereotype.Component
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.questmission.application.QuestEventProcessor
import systems.zlink.samples.kotlin.gamequest.shared.contracts.NotifyQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.NotifyQuestProgressRes

@Component
class ZLinkQuestProgressNotifier(
    private val channels: ZLinkClient,
) : QuestEventProcessor.QuestProgressNotifier {
    override fun notify(sourceApi: String, request: NotifyQuestProgressReq): Boolean {
        val apiName = if (sourceApi == "api-b") "api-b" else "api-a"
        return try {
            val delivered = channels
                .requestToChannel(SampleNames.gameApiActionChannel(apiName), request)
                .timeout(SampleNames.RequestTimeout)
                .await(NotifyQuestProgressRes::class.java)
            delivered != null && delivered.delivered
        } catch (error: RuntimeException) {
            System.err.printf(
                "gamequest mission projection kept while stream notify failed. player=%s%n",
                request.playerId,
            )
            false
        }
    }
}
