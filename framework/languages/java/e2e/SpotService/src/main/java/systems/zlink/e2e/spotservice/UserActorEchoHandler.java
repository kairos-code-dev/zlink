package systems.zlink.e2e.spotservice;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class UserActorEchoHandler {
    @ZLinkSpotActorRequest(packetName = "ActorEchoRequest")
    public Contracts.ActorEchoReply handle(
        UserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorEchoRequest request,
        CancellationToken cancellationToken) {
        int seq = actor.nextSequence();
        spot.record("ActorUserRequest", actor.actorId() + "/" + request.value() + "#" + seq);
        actor.context().boundSession()
            .send(new Contracts.ActorPush(actor.actorId(), spot.spotRid(), "push:" + request.value(), request.seq(), seq))
            .submit();
        return new Contracts.ActorEchoReply(
            actor.actorId(),
            spot.spotRid(),
            spot.nodeRid(),
            "user:" + request.value(),
            request.seq(),
            seq,
            request.profile().displayName(),
            request.profile().level(),
            request.profile().tags());
    }
}
