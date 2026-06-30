package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.session.spots.ScenarioActor
import systems.zlink.e2e.kotlin.spotservice.session.spots.UserSpot
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext

class UserActorEchoHandler {
    @ZLinkSpotActorRequest(packetName = "ActorEchoRequest")
    fun handle(
        spot: UserSpot,
        actor: ScenarioActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.ActorEchoRequest,
        cancellationToken: CancellationToken
    ): Contracts.ActorEchoReply {
        val seq = actor.nextSequence()
        spot.record("ActorUserRequest", actor.actorId() + "/" + request.value + "#" + seq)
        actor.context().boundSession()
            .send(Contracts.ActorPush(actor.actorId(), spot.spotRid(), "push:" + request.value, request.seq, seq))
            .submit()
        return Contracts.ActorEchoReply(
            actor.actorId(),
            spot.spotRid(),
            spot.nodeRid(),
            "user:" + request.value,
            request.seq,
            seq,
            request.profile.displayName,
            request.profile.level,
            request.profile.tags
        )
    }
}
