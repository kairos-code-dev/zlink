package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class MultiNodeStateReqHandler {
    @ZLinkSpotRequest
    public Contracts.MultiNodeStateRes handle(
        MultiNodeSpot spot,
        Contracts.MultiNodeStateReq request) {
        int value = spot.add(request.delta());
        return new Contracts.MultiNodeStateRes(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            value);
    }
}
