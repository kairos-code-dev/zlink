package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class ProbeActorFastHandler {
    private final PlayEvidenceStore evidence;

    public ProbeActorFastHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotActorRequest(packetName = "ActorFastReq")
    public Contracts.ActorRes handle(
        ProbeSpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorFastReq request,
        CancellationToken cancellationToken) {
        String value = "actor=" + actor.actorId() + ";marker=" + request.marker()
            + ";spot=" + spot.context().spotRid();
        evidence.record(request.requestId(), "actor-fast-started", value);
        evidence.record(request.requestId(), "actor-fast-completed", value);
        return new Contracts.ActorRes(
            "YD-B",
            request.requestId(),
            actor.actorId(),
            request.marker());
    }
}
