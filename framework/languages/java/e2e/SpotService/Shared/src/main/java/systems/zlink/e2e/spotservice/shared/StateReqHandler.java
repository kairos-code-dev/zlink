package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class StateReqHandler {
    @ZLinkSpotRequest
    public Contracts.StateRes handle(
        UserSpot spot,
        Contracts.StateReq request) {
        String value = request.op().equals("worker-start")
            ? spot.startWorker(request.op())
            : spot.apply(request.op());
        return new Contracts.StateRes(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            value);
    }
}
