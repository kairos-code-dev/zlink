package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers;

import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoWinnerEventHandler
    implements ZLinkSpotSubscriptionHandler<BingoRoomSpot, Messages.BingoWinnerEvent> {
    @Override
    public void handle(BingoRoomSpot spot, Messages.BingoWinnerEvent message) {
        spot.announceWinner(message);
    }
}
