package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers;

import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;

public final class BingoRoomTimerHandler implements ZLinkSpotTimerHandler<BingoRoomSpot> {
    @Override
    public void handle(BingoRoomSpot spot, ZLinkTimerTick tick) {
        spot.tick();
    }
}
