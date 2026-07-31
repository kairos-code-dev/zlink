package systems.zlink.e2e.kotlin.spotservice.play.spots

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.handlers.*
import systems.zlink.framework.kotlin.addHandler
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpot
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResult

class ScenarioEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val evidence: ScenarioState
) : ZLinkSuspendingEntrySpot<ScenarioActor>() {
    override fun context(): ZLinkEntrySpotContext = context

    fun nodeRid(): String = evidence.nodeRid()

    fun record(marker: String, value: String) {
        evidence.record(marker, "entry", value)
    }

    override fun configure() {
        context.handlers().addHandler<EntryActorEchoHandler>()
        context.handlers().addHandler<EntryActorJoinHandler>()
    }

    override suspend fun onCreateActorSuspending(
        actor: ScenarioActor,
        createRequest: ZLinkMessage,
    ) {
        if (!createRequest.isEmpty) {
            val request = createRequest.decode(Contracts.ActorAuthReq::class.java)
            actor.applyProfile(request.profile)
            evidence.record(
                "ActorCreatedPayload",
                "entry",
                request.profile.displayName +
                    "/" + request.profile.level +
                    "/" + request.profile.tags.joinToString(",")
            )
        }
        evidence.record("ActorCreated", "entry", actor.actorId() + "#" + actor.nextSequence())
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResult {
        evidence.record("ActorEntryJoinRequested", "entry", actorId)
        return ZLinkSpotActorJoinResult.accept()
    }

    override suspend fun onJoinedActorSuspending(actor: ScenarioActor) {
        evidence.record("ActorEntryJoined", "entry", actor.actorId() + "#" + actor.nextSequence())
    }

    override suspend fun onLeaveActorSuspending(actor: ScenarioActor) {
    }
}
