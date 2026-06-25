package systems.zlink.e2e.spotservice;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinRequest")
    public Contracts.ActorJoinReply handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorJoinRequest request,
        CancellationToken cancellationToken) {
        actor.applyProfile(request.profile());
        spot.record("ActorJoinPayload", payloadEvidence(request));
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .await(Contracts.ActorJoinReply.class)
            .reply();
    }

    private static String payloadEvidence(Contracts.ActorJoinRequest request) {
        return request.profile().displayName()
            + "/" + request.profile().level()
            + "/" + String.join(",", request.tags());
    }
}
