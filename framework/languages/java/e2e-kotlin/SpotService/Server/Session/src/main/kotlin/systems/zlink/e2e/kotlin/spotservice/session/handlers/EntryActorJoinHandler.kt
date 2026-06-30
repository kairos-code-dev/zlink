package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.session.spots.ScenarioActor
import systems.zlink.e2e.kotlin.spotservice.session.spots.ScenarioEntrySpot
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext

class EntryActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinReq")
    fun handle(
        spot: ScenarioEntrySpot,
        actor: ScenarioActor,
        context: ZLinkSpotActorRequestContext,
        request: Contracts.ActorJoinReq,
        cancellationToken: CancellationToken
    ): Contracts.ActorJoinRes {
        actor.applyProfile(request.profile)
        spot.record("ActorJoinPayload", payloadEvidence(request))
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid), request)
            .await(Contracts.ActorJoinRes::class.java)
            .reply()
    }

    private fun payloadEvidence(request: Contracts.ActorJoinReq): String {
        return request.profile.displayName +
            "/" + request.profile.level +
            "/" + request.tags.joinToString(",")
    }
}
