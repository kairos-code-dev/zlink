package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class StageTimerStartReqHandler {
    @ZLinkSpotRequest
    public Contracts.StageTimerStartRes handle(
        UserSpot spot,
        Contracts.StageTimerStartReq request) {
        return spot.stage().startTimer(request);
    }
}
