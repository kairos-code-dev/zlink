package systems.zlink.e2e.spotservice;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class StateRequestHandler {
    @ZLinkSpotRequest
    public Contracts.StateReply handle(
        UserSpot spot,
        Contracts.StateRequest request) {
        return new Contracts.StateReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            spot.apply(request.op()));
    }
}
