package systems.zlink.samples.kotlin.gamequest.server.gameapi.infrastructure.zlink

import org.springframework.stereotype.Component
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.gameapi.application.GameplayActionService
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameplayEventMsg

@Component
class ZLinkGameplayEventPublisher(
    private val fanout: ZLinkFanoutClient,
) : GameplayActionService.GameplayEventPublisher {
    override fun publish(event: GameplayEventMsg) {
        fanout.publish(SampleNames.FanoutChannel, SampleNames.GameplayTopic, event).await()
    }
}
