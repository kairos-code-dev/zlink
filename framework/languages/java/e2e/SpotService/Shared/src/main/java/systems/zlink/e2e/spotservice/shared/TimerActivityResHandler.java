package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class TimerActivityResHandler {
    @ZLinkSpotRequest
    public Contracts.TimerActivityRes handle(
        TimerScenarioSpot spot,
        String request) {
        return spot.status();
    }
}
