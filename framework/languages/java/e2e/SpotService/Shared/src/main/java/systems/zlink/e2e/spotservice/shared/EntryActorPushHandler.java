package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorPushHandler {
    @ZLinkSpotActorRequest(packetName = "ActorPushReq")
    public Contracts.ActorPingRes handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorPushReq request,
        CancellationToken cancellationToken) {
        int seq = actor.nextSequence();
        spot.record("ActorPushReq", actor.actorId() + "/" + request.value() + "#" + seq);
        actor.context().boundSession()
            .send(new Contracts.ActorPushNotify(actor.actorId(), "entry", request.value(), seq, seq))
            .submit();
        return new Contracts.ActorPingRes(
            actor.actorId(),
            spot.nodeRid(),
            "entry",
            request.value(),
            seq);
    }
}
