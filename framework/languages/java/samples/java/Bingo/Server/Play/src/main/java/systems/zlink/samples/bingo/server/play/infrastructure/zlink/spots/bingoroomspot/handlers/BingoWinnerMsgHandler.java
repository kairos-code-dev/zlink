package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers;

import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoWinnerMsgHandler
    implements ZLinkSpotSubscriptionHandler<BingoRoomSpot, Messages.BingoWinnerMsg> {
    @Override
    public void handle(BingoRoomSpot spot, Messages.BingoWinnerMsg message) {
        spot.announceWinner(message);
    }
}
