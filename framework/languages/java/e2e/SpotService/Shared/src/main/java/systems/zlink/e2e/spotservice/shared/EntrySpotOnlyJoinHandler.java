package systems.zlink.e2e.spotservice.shared;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntrySpotOnlyJoinHandler {
    @ZLinkSpotActorRequest(packetName = "SpotOnlyJoinReq")
    public Contracts.SpotOnlyJoinRes handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.SpotOnlyJoinReq request,
        CancellationToken cancellationToken) {
        if (!actor.actorId().equals(request.actorId())) {
            throw new IllegalStateException("spot-only join actor does not match dispatched actor");
        }
        var joined = actor.context()
            .joinSpot(RoutingId.from(request.targetSpotRid()))
            .await();
        spot.record(
            "SpotOnlyActorJoin",
            actor.actorId() + "/" + request.targetSpotRid() + "/" + joined.accepted() + "/" + request.marker());
        return new Contracts.SpotOnlyJoinRes(
            request.targetSpotRid(),
            actor.actorId(),
            joined.accepted(),
            request.marker());
    }
}
