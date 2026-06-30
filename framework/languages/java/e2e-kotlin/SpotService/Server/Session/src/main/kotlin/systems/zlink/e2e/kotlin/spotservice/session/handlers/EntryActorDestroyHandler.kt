package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.session.spots.ScenarioActor
import systems.zlink.e2e.kotlin.spotservice.session.spots.ScenarioEntrySpot
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext

class EntryActorDestroyHandler {
    @ZLinkSpotActorRequest(packetName = "DestroyActorRequest")
    fun handle(
        spot: ScenarioEntrySpot,
        actor: ScenarioActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.DestroyActorRequest,
        cancellationToken: CancellationToken
    ): Contracts.DestroyActorReply {
        if (request.actorId != actor.actorId()) {
            throw IllegalStateException("destroy request actor does not match dispatched actor")
        }

        spot.context().runWorker { true }
            .submit(
                { _, _ ->
                    try {
                        spot.context().destroyActor(actor).toCompletableFuture().join()
                        spot.record("ActorDestroyed", actor.actorId())
                    } catch (error: Exception) {
                        spot.record("ActorDestroyFailed", actor.actorId() + "/" + error.javaClass.simpleName)
                    }
                },
                { error, _ ->
                    spot.record("ActorDestroyFailed", actor.actorId() + "/" + error.javaClass.simpleName)
                }
            )

        return Contracts.DestroyActorReply(actor.actorId(), true)
    }
}
