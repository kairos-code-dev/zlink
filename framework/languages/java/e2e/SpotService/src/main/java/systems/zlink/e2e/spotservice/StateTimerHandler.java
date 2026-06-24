package systems.zlink.e2e.spotservice;

import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;

public final class StateTimerHandler implements ZLinkSpotTimerHandler<UserSpot> {
    @Override
    public void handle(
        UserSpot spot,
        ZLinkTimerTick tick) {
        spot.timerTick(tick.deliveryIndex());
    }
}
