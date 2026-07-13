package systems.zlink.e2e.spotservice.shared;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorDestroyHandler {
    @ZLinkSpotActorRequest(packetName = "ActorDestroyReq")
    public CompletionStage<Contracts.ActorDestroyRes> handle(
        ScenarioEntrySpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorDestroyReq request) {
        if (!request.actorId().equals(actor.actorId())) {
            throw new IllegalStateException("destroy request actor does not match dispatched actor");
        }
        return spot.context().destroyActor(actor).thenApply(ignored -> {
            spot.record("ActorDestroyed", actor.actorId());
            return new Contracts.ActorDestroyRes(actor.actorId(), true);
        });
    }
}
