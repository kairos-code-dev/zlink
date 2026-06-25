package systems.zlink.e2e.kotlin.spotservice;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class TimerStatusHandler {
    @ZLinkSpotRequest
    public Contracts.TimerStatus handle(
        TimerScenarioSpot spot,
        String request) {
        return spot.status();
    }
}
