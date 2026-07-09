package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorJoinYieldHandler {
    private final PlayEvidenceStore evidence;

    public EntryActorJoinYieldHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @ZLinkSpotActorRequest(packetName = "ActorJoinYieldReq")
    public Contracts.ActorRes handle(
        ProbeEntrySpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorJoinYieldReq request,
        CancellationToken cancellationToken) {
        String value = "actor=" + actor.actorId() + ";spot=" + spot.context().spotRid()
            + ";target=" + request.targetSpotRid();
        evidence.record(request.requestId(), "actor-join-yield-started", value);
        evidence.record(request.requestId(), "actor-join-yield-released", value);
        ZLinkActorJoinResult<Void> joined = actor.context()
            .joinSpot(
                RoutingId.from(request.targetSpotRid()),
                new Contracts.DelayReq(request.requestId(), 350))
            .yield();
        String completed = value + ";accepted=" + joined.accepted()
            + ";joined=" + joined.actor().actorId();
        evidence.record(request.requestId(), "actor-join-yield-resumed", completed);
        evidence.record(request.requestId(), "actor-join-yield-completed", completed);
        return new Contracts.ActorRes(
            "YD-B3",
            request.requestId(),
            actor.actorId(),
            "actor-join-yield-completed");
    }
}
