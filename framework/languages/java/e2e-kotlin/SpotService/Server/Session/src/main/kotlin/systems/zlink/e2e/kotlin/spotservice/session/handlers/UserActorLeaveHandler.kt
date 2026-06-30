package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.session.spots.ScenarioActor
import systems.zlink.e2e.kotlin.spotservice.session.spots.UserSpot
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext

class UserActorLeaveHandler {
    @ZLinkSpotActorRequest(packetName = "LeaveActorRequest")
    fun handle(
        spot: UserSpot,
        actor: ScenarioActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.LeaveActorRequest,
        cancellationToken: CancellationToken
    ): Contracts.LeaveActorReply {
        if (request.actorId != actor.actorId()) {
            throw IllegalStateException("leave request actor does not match dispatched actor")
        }
        spot.context().leaveActor(actor).toCompletableFuture().join()
        return Contracts.LeaveActorReply(actor.actorId(), true)
    }
}
