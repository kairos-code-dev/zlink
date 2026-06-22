package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.handlers;

import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.PlayEntrySpot;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerWinMilestoneEvent;

public final class PlayerWinMilestoneEventHandler
    implements ZLinkSpotSubscriptionHandler<PlayEntrySpot, PlayerWinMilestoneEvent> {
    @Override
    public void handle(PlayEntrySpot spot, PlayerWinMilestoneEvent message) {
        spot.notifyMilestone(message);
    }
}
