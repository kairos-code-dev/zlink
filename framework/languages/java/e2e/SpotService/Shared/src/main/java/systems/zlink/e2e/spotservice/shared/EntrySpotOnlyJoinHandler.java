package systems.zlink.e2e.spotservice.shared;

import systems.zlink.contracts.core.RoutingId;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResult;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.ZLinkMessageContext;

public final class EntrySpotOnlyJoinHandler {
    @ZLinkSpotActorRequest(packetName = "SpotOnlyJoinReq")
    public CompletionStage<Contracts.SpotOnlyJoinRes> handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkMessageContext context,
        Contracts.SpotOnlyJoinReq request) {
        if (!actor.actorId().equals(request.actorId())) {
            throw new IllegalStateException("spot-only join actor does not match dispatched actor");
        }
        return actor.context()
            .joinSpot(RoutingId.from(request.targetSpotRid()), request)
            .submit()
            .thenApply(joined -> {
                boolean accepted = joined instanceof ZLinkSpotActorJoinResult.Accepted<?>;
                spot.record(
                    "SpotOnlyActorJoin",
                    actor.actorId() + "/" + request.targetSpotRid() + "/" + accepted + "/" + request.marker());
                return new Contracts.SpotOnlyJoinRes(
                    request.targetSpotRid(), actor.actorId(), accepted, request.marker());
            });
    }
}
