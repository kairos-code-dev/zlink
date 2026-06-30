package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class SlowRequestHandler {
    @ZLinkSpotRequest
    public Contracts.StateReply handle(
        UserSpot spot,
        Contracts.SlowRequest request) {
        try {
            Thread.sleep(1000);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted", error);
        }
        return new Contracts.StateReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            spot.apply("slow:" + request.value()));
    }
}
