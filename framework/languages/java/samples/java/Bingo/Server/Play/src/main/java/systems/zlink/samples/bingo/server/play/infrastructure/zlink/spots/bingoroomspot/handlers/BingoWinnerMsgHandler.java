package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers;

import systems.zlink.framework.handlers.ZLinkSpotSubscription;
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkSpotSubscription(topic = SampleNames.WinnerTopic)
public final class BingoWinnerMsgHandler
    implements ZLinkSpotSubscriptionHandler<BingoRoomSpot, Messages.BingoWinnerMsg> {
    @Override
    public java.util.concurrent.CompletionStage<Void> handle(
        BingoRoomSpot spot,
        Messages.BingoWinnerMsg message) {
        spot.announceWinner(message);
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }
}
