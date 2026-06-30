package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class ProbeActorYieldHandler {
    private final PlayEvidenceStore evidence;

    public ProbeActorYieldHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotActorRequest(packetName = "ActorYieldReq")
    public Contracts.ActorRes handle(
        ProbeSpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorYieldReq request,
        CancellationToken cancellationToken) {
        String value = "actor=" + actor.actorId() + ";spot=" + spot.context().spotRid();
        evidence.record(request.requestId(), "actor-yield-started", value);
        evidence.record(request.requestId(), "actor-yield-released", value);
        spot.context().outbound()
            .requestToChannel(
                Contracts.DELAY_CHANNEL,
                new Contracts.DelayReq(request.requestId(), request.delayMillis()))
            .timeout(Duration.ofSeconds(5))
            .yield(Contracts.DelayRes.class);
        evidence.record(request.requestId(), "actor-yield-resumed", value);
        evidence.record(request.requestId(), "actor-yield-completed", value);
        return new Contracts.ActorRes(
            "YD-B",
            request.requestId(),
            actor.actorId(),
            "actor-yield-completed");
    }
}
