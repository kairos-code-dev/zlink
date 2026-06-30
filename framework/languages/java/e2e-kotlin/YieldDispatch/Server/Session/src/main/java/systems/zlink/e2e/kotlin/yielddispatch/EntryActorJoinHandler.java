package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinReq")
    public Contracts.ActorJoinRes handle(
        ProbeEntrySpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorJoinReq request,
        CancellationToken cancellationToken) {
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .await(Contracts.ActorJoinRes.class)
            .reply();
    }
}
