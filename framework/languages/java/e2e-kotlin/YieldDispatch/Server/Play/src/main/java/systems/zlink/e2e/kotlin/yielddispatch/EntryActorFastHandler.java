package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorFastHandler {
    private final PlayEvidenceStore evidence;

    public EntryActorFastHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotActorRequest(packetName = "ActorFastRequest")
    public Contracts.ActorReply handle(
        ProbeEntrySpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorFastRequest request,
        CancellationToken cancellationToken) {
        String value = "actor=" + actor.actorId() + ";marker=" + request.marker()
            + ";spot=" + spot.context().spotRid();
        evidence.record(request.requestId(), "actor-fast-started", value);
        evidence.record(request.requestId(), "actor-fast-completed", value);
        return new Contracts.ActorReply(
            "YD-B",
            request.requestId(),
            actor.actorId(),
            request.marker());
    }
}
