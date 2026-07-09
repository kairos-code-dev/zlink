package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorPushYieldHandler {
    private final PlayEvidenceStore evidence;

    public EntryActorPushYieldHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotActorRequest(packetName = "ActorPushYieldReq")
    public Contracts.ActorRes handle(
        ProbeEntrySpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorPushYieldReq request,
        CancellationToken cancellationToken) {
        String value = "actor=" + actor.actorId() + ";spot=" + spot.context().spotRid();
        evidence.record(request.requestId(), "actor-push-yield-started", value);
        evidence.record(request.requestId(), "actor-push-yield-released", value);
        spot.context().outbound()
            .requestToChannel(
                Contracts.DELAY_CHANNEL,
                new Contracts.DelayReq(request.requestId(), request.delayMillis()))
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.DelayRes.class);
        evidence.record(request.requestId(), "actor-push-yield-resumed", value);
        actor.context().boundSession()
            .send(new Contracts.ActorPushNotify(
                actor.actorId(),
                request.requestId(),
                request.value(),
                spot.context().nodeRid().toString()))
            .packetName("ActorPushNotify")
            .await();
        evidence.record(request.requestId(), "actor-push-yield-completed", value);
        return new Contracts.ActorRes(
            "YD-D4",
            request.requestId(),
            actor.actorId(),
            "actor-push-yield-completed");
    }
}
