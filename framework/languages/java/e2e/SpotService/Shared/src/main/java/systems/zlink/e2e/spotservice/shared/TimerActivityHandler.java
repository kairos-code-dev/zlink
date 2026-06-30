package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class TimerActivityHandler {
    @ZLinkSpotRequest
    public Contracts.TimerStatus handle(
        TimerScenarioSpot spot,
        Contracts.TimerActivity request) {
        spot.activity(request.value());
        return spot.status();
    }
}
