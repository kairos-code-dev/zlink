package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;

public final class TimerOverrunHandler implements ZLinkSpotTimerHandler<TimerScenarioSpot> {
    @Override
    public void handle(
        TimerScenarioSpot spot,
        ZLinkTimerTick tick) {
        try {
            Thread.sleep(160);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("timer overrun interrupted", error);
        }
        spot.overrunTick(tick.deliveryIndex(), tick.skippedTicks());
    }
}
