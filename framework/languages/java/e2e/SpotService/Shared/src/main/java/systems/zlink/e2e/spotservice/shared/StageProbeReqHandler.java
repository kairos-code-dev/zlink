package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class StageProbeReqHandler {
    @ZLinkSpotRequest
    public Contracts.StateRes handle(
        UserSpot spot,
        Contracts.StageProbeReq request) {
        return spot.stage().apply(request);
    }
}
