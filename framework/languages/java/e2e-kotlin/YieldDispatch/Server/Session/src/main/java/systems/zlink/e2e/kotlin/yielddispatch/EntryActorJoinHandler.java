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
        actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .yield(Contracts.ActorJoinRes.class);
        return new Contracts.ActorJoinRes(
            actor.actorId(),
            request.spotRid(),
            "joined:" + request.value());
    }
}
