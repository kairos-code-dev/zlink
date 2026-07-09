package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;

public final class StageTimerHandler implements ZLinkSpotTimerHandler<UserSpot> {
    @Override
    public void handle(
        UserSpot spot,
        ZLinkTimerTick tick) {
        spot.record("StageTimer", tick.name());
    }
}
