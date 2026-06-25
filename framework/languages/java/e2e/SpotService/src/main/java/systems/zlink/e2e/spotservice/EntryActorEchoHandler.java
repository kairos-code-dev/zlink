package systems.zlink.e2e.spotservice;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorEchoHandler {
    @ZLinkSpotActorRequest(packetName = "ActorEchoRequest")
    public Contracts.ActorEchoReply handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorEchoRequest request,
        CancellationToken cancellationToken) {
        int seq = actor.nextSequence();
        spot.record("ActorEntryRequest", actor.actorId() + "/" + request.value() + "#" + seq);
        actor.context().boundSession()
            .send(new Contracts.ActorPush(actor.actorId(), "entry", "push:" + request.value(), seq))
            .submit();
        return new Contracts.ActorEchoReply(
            actor.actorId(),
            "entry",
            spot.nodeRid(),
            "entry:" + request.value(),
            seq,
            request.profile().tags());
    }
}
