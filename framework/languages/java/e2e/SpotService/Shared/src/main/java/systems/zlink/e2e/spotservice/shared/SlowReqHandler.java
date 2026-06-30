package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class SlowReqHandler {
    @ZLinkSpotRequest
    public Contracts.StateRes handle(
        UserSpot spot,
        Contracts.SlowReq request) {
        try {
            Thread.sleep(1000);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
        return new Contracts.StateRes(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            spot.apply("slow:" + request.value()));
    }
}
