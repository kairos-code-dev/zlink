package systems.zlink.e2e.spotservice.shared;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorJoinAdmissionHandler {
    @ZLinkSpotActorRequest(packetName = "JoinAdmittedUserSpotActorReq")
    public Contracts.JoinAdmittedUserSpotActorRes handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.JoinAdmittedUserSpotActorReq request,
        CancellationToken cancellationToken) {
        actor.applyProfile(request.profile());
        try {
            Contracts.ActorJoinRes joined = actor.context()
                .joinSpot(RoutingId.from(request.spotRid()), request)
                .await(Contracts.ActorJoinRes.class)
                .reply();
            return new Contracts.JoinAdmittedUserSpotActorRes(
                actor.actorId(),
                joined.spotRid(),
                joined.nodeRid(),
                true,
                "");
        } catch (RuntimeException error) {
            if (!isActorJoinRejected(error)) {
                throw error;
            }
            return new Contracts.JoinAdmittedUserSpotActorRes(
                actor.actorId(),
                request.spotRid(),
                spot.nodeRid(),
                false,
                "ActorJoinRejected");
        }
    }

    private static boolean isActorJoinRejected(Throwable error) {
        Throwable current = error;
        while (current != null) {
            String message = current.getMessage();
            if (message != null && message.startsWith("actor spot join rejected:")) {
                return true;
            }
            current = current.getCause();
        }
        return false;
    }
}
