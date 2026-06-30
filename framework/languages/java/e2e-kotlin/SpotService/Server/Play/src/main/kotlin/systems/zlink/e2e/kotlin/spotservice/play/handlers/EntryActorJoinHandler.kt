package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext

class EntryActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinRequest")
    fun handle(
        spot: ScenarioEntrySpot,
        actor: ScenarioActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.ActorJoinRequest,
        cancellationToken: CancellationToken
    ): Contracts.ActorJoinReply {
        actor.applyProfile(request.profile)
        spot.record("ActorJoinPayload", payloadEvidence(request))
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid), request)
            .await(Contracts.ActorJoinReply::class.java)
            .reply()
    }

    private fun payloadEvidence(request: Contracts.ActorJoinRequest): String {
        return request.profile.displayName +
            "/" + request.profile.level +
            "/" + request.tags.joinToString(",")
    }
}
