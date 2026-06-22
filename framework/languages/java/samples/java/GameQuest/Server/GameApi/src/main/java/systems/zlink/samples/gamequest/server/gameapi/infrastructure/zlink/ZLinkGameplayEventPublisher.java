package systems.zlink.samples.gamequest.server.gameapi.infrastructure.zlink;

import org.springframework.stereotype.Component;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@Component
public final class ZLinkGameplayEventPublisher implements GameplayActionService.GameplayEventPublisher {
    private final ZLinkFanoutClient fanout;

    public ZLinkGameplayEventPublisher(ZLinkFanoutClient fanout) {
        this.fanout = fanout;
    }

    @Override
    public void publish(Messages.GameplayEventEnvelope event) {
        fanout.publish(SampleNames.FanoutChannel, SampleNames.GameplayTopic, event).await();
    }
}
