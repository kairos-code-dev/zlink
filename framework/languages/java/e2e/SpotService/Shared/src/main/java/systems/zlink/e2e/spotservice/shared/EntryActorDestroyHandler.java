package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorDestroyHandler {
    @ZLinkSpotActorRequest(packetName = "ActorDestroyReq")
    public Contracts.ActorDestroyRes handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorDestroyReq request,
        CancellationToken cancellationToken) {
        if (!request.actorId().equals(actor.actorId())) {
            throw new IllegalStateException("destroy request actor does not match dispatched actor");
        }
        spot.context().destroyActor(actor).toCompletableFuture().join();
        spot.record("ActorDestroyed", actor.actorId());
        return new Contracts.ActorDestroyRes(actor.actorId(), true);
    }
}
