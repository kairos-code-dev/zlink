package systems.zlink.e2e.spotservice.shared;

import systems.zlink.contracts.core.RoutingId;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinReq")
    public CompletionStage<Contracts.ActorJoinRes> handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorJoinReq request) {
        actor.applyProfile(request.profile());
        spot.record("ActorJoinPayload", payloadEvidence(request));
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .submit(Contracts.ActorJoinRes.class)
            .thenApply(response -> response.reply());
    }

    private static String payloadEvidence(Contracts.ActorJoinReq request) {
        return request.profile().displayName()
            + "/" + request.profile().level()
            + "/" + String.join(",", request.tags());
    }
}
