package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinRequest")
    public Contracts.ActorJoinReply handle(
        ProbeEntrySpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorJoinRequest request,
        CancellationToken cancellationToken) {
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .await(Contracts.ActorJoinReply.class)
            .reply();
    }
}
