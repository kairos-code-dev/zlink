package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class ProbeRequestHandler {
    @ZLinkSpotRequest
    public Contracts.ProbeReply handle(
        ProbeSpot spot,
        Contracts.ProbeRequest request) {
        return spot.handle(request);
    }
}
