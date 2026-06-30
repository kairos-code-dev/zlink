package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class TimerActivityReqHandler {
    @ZLinkSpotRequest
    public Contracts.TimerActivityRes handle(
        TimerScenarioSpot spot,
        Contracts.TimerActivityReq request) {
        spot.activity(request.value());
        return spot.status();
    }
}
