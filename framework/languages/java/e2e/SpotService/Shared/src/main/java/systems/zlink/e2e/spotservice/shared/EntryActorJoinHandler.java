package systems.zlink.e2e.spotservice.shared;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinReq")
    public Contracts.ActorJoinRes handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorJoinReq request,
        CancellationToken cancellationToken) {
        actor.applyProfile(request.profile());
        spot.record("ActorJoinPayload", payloadEvidence(request));
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .await(Contracts.ActorJoinRes.class)
            .reply();
    }

    private static String payloadEvidence(Contracts.ActorJoinReq request) {
        return request.profile().displayName()
            + "/" + request.profile().level()
            + "/" + String.join(",", request.tags());
    }
}
