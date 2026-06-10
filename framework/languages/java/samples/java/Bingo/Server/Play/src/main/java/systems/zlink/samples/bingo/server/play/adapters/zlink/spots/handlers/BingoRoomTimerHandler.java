package systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoRoomSpot;

public final class BingoRoomTimerHandler implements ZLinkSpotTimerHandler<BingoRoomSpot> {
    @Override
    public CompletionStage<Void> handleAsync(BingoRoomSpot spot, ZLinkTimerTick tick) {
        return spot.tickAsync();
    }
}
