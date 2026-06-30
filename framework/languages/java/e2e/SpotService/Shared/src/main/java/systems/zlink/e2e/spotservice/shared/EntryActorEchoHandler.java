package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorEchoHandler {
    @ZLinkSpotActorRequest(packetName = "ActorEchoReq")
    public Contracts.ActorEchoRes handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorEchoReq request,
        CancellationToken cancellationToken) {
        int seq = actor.nextSequence();
        spot.record("ActorEntryReq", actor.actorId() + "/" + request.value() + "#" + seq);
        actor.context().boundSession()
            .send(new Contracts.ActorPushNotify(actor.actorId(), "entry", "push:" + request.value(), request.seq(), seq))
            .submit();
        return new Contracts.ActorEchoRes(
            actor.actorId(),
            "entry",
            spot.nodeRid(),
            "entry:" + request.value(),
            request.seq(),
            seq,
            request.profile().displayName(),
            request.profile().level(),
            request.profile().tags());
    }
}
