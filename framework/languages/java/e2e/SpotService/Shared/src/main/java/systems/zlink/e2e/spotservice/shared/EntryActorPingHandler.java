package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorPingHandler {
    @ZLinkSpotActorRequest(packetName = "ActorPingReq")
    public Contracts.ActorPingRes handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorPingReq request,
        CancellationToken cancellationToken) {
        int seq = actor.nextSequence();
        spot.record("ActorPingReq", actor.actorId() + "/" + request.value() + "#" + seq);
        return new Contracts.ActorPingRes(
            actor.actorId(),
            spot.nodeRid(),
            "entry",
            request.value(),
            seq);
    }
}
