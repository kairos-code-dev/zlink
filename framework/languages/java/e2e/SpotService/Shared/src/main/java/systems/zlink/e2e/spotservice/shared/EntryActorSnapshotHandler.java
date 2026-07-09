package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorSnapshotHandler {
    @ZLinkSpotActorRequest(packetName = "SnapshotReq")
    public Contracts.SnapshotRes handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.SnapshotReq request,
        CancellationToken cancellationToken) {
        if (!actor.actorId().equals(request.actorId())) {
            throw new IllegalStateException("Snapshot request actor does not match dispatched actor.");
        }
        return new Contracts.SnapshotRes(actor.actorId(), actor.currentSequence());
    }
}
