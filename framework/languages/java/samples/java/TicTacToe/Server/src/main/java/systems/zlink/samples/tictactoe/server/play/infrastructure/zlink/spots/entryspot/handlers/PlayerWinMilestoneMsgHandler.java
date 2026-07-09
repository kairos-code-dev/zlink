package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.entryspot.handlers;

import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerWinMilestoneMsg;

@ZLinkSpotSubscription(topic = SampleNames.PlayerMilestoneTopic)
public final class PlayerWinMilestoneMsgHandler
    implements ZLinkSpotSubscriptionHandler<PlayEntrySpot, PlayerWinMilestoneMsg> {
    @Override
    public void handle(PlayEntrySpot spot, PlayerWinMilestoneMsg message) {
        spot.notifyMilestone(message);
    }
}
