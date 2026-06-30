package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class ProbeReqHandler {
    @ZLinkSpotRequest
    public Contracts.ProbeRes handle(
        ProbeSpot spot,
        Contracts.ProbeReq request) {
        return spot.handle(request);
    }
}
