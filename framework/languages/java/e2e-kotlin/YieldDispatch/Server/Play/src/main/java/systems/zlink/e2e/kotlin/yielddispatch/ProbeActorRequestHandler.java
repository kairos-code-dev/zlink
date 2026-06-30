package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class ProbeActorRequestHandler {
    @ZLinkSpotActorRequest(packetName = "ProbeRequest")
    public Contracts.ProbeReply handle(
        ProbeSpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ProbeRequest request,
        CancellationToken cancellationToken) {
        return spot.handle(request);
    }
}
