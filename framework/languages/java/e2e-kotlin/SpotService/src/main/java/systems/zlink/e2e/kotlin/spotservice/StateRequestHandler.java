package systems.zlink.e2e.kotlin.spotservice;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class StateRequestHandler {
    @ZLinkSpotRequest
    public Contracts.StateReply handle(
        UserSpot spot,
        Contracts.StateRequest request) {
        String value = request.op().equals("worker-start")
            ? spot.startWorker(request.op())
            : spot.apply(request.op());
        return new Contracts.StateReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            value);
    }
}
