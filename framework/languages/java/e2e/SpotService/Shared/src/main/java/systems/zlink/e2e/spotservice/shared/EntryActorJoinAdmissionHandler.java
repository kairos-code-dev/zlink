package systems.zlink.e2e.spotservice.shared;

import systems.zlink.contracts.core.RoutingId;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResult;
import systems.zlink.framework.ZLinkMessageContext;

public final class EntryActorJoinAdmissionHandler {
    @ZLinkSpotActorRequest(packetName = "JoinAdmittedUserSpotActorReq")
    public CompletionStage<Contracts.JoinAdmittedUserSpotActorRes> handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkMessageContext context,
        Contracts.JoinAdmittedUserSpotActorReq request) {
        actor.applyProfile(request.profile());
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .submit(Contracts.ActorJoinRes.class)
            .thenApply(result -> {
                if (result instanceof ZLinkSpotActorJoinResult.Accepted<Contracts.ActorJoinRes> accepted) {
                    Contracts.ActorJoinRes joined = accepted.reply();
                    return new Contracts.JoinAdmittedUserSpotActorRes(
                        actor.actorId(), joined.spotId(), joined.nodeRid(), true, "");
                }
                return new Contracts.JoinAdmittedUserSpotActorRes(
                    actor.actorId(), request.spotRid(), spot.nodeRid(), false, "ActorJoinRejected");
            });
    }
}
