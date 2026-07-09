package systems.zlink.e2e.kotlin.spotservice.session.spots

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.session.handlers.EntryActorDestroyHandler
import systems.zlink.e2e.kotlin.spotservice.session.handlers.EntryActorEchoHandler
import systems.zlink.e2e.kotlin.spotservice.session.handlers.EntryActorJoinHandler
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.addHandler
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse

class ScenarioEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val evidence: ScenarioState
) : ZLinkEntrySpot<ScenarioActor> {
    override fun context(): ZLinkEntrySpotContext = context

    fun nodeRid(): String = evidence.nodeRid()

    fun record(marker: String, value: String) {
        evidence.record(marker, "entry", value)
    }

    override fun configure() {
        context.handlers().addHandler<EntryActorEchoHandler>()
        context.handlers().addHandler<EntryActorJoinHandler>()
        context.handlers().addHandler<EntryActorDestroyHandler>()
    }

    override fun onCreateActor(
        actor: ScenarioActor,
        createRequest: ZLinkMessage,
        cancellationToken: CancellationToken
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

    override fun onActorJoin(
        actor: ScenarioActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken
    ): ZLinkSpotActorJoinResponse {
        evidence.record("ActorEntryJoinRequested", "entry", actor.actorId() + "#" + actor.nextSequence())
        return ZLinkSpotActorJoinResponse.accept()
    }

    override fun onJoinedActor(
        actor: ScenarioActor,
        cancellationToken: CancellationToken
    ) {
        evidence.record("ActorEntryJoined", "entry", actor.actorId() + "#" + actor.nextSequence())
    }

    override fun onDisconnectActor(
        actor: ScenarioActor,
        cancellationToken: CancellationToken
    ) {
        evidence.record("ActorEntryDisconnected", "entry", actor.actorId())
    }
}
