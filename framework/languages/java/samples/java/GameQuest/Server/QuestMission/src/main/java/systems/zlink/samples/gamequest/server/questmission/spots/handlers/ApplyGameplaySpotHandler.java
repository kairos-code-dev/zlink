package systems.zlink.samples.gamequest.server.questmission.spots.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestSpot;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

public final class ApplyGameplaySpotHandler
    implements ZLinkSpotPacketHandler<PlayerQuestSpot, Messages.GameplayMsg> {
    private final ZLinkClient channels;

    public ApplyGameplaySpotHandler(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public CompletionStage<Void> handle(
        PlayerQuestSpot spot,
        Messages.GameplayMsg request) {
        Messages.QuestProcessingMsg result = spot.apply(request);
        channels.sendToChannel(
                SampleNames.questNotificationChannelFor(request.sourceApi()),
                result)
            .submit();
        return CompletableFuture.completedFuture(null);
    }
}
