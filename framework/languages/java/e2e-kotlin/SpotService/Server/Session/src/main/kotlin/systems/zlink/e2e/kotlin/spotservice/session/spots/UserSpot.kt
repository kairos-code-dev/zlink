package systems.zlink.e2e.kotlin.spotservice.session.spots

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.session.handlers.UserActorEchoHandler
import systems.zlink.e2e.kotlin.spotservice.session.handlers.UserActorLeaveHandler
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse

class UserSpot(
    private val context: ZLinkSpotContext,
    private val evidence: ScenarioState
) : ZLinkSpot<ScenarioActor> {
    override fun context(): ZLinkSpotContext = context

    override fun configure() {
        context.handlers().addActorRequest(UserActorEchoHandler::class.java)
        context.handlers().addActorRequest(UserActorLeaveHandler::class.java)
    }

    override fun onCreate(request: ZLinkMessage): ZLinkSpotCreateResponse {
        evidence.record("SpotCreated", context.spotRid().toString(), if (request.isEmpty) "" else "request")
        return ZLinkSpotCreateResponse.accept()
    }

    override fun onInitialize() {
        evidence.record("SpotInitialized", context.spotRid().toString(), "")
    }

    override fun onClosing() {
        evidence.record("SpotClosing", context.spotRid().toString(), "")
    }

    override fun onActorJoin(
        actor: ScenarioActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken
    ): ZLinkSpotActorJoinResponse {
        val join = request.decode(Contracts.ActorJoinReq::class.java)
        actor.applyProfile(join.profile)
        evidence.record(
            "ActorUserJoinRequested",
            context.spotRid().toString(),
            actor.actorId() + "/" + join.profile.displayName + "/" + join.tags.joinToString(",")
        )
        return ZLinkSpotActorJoinResponse.accept(
            Contracts.ActorJoinRes(
                actor.actorId(),
                context.spotRid().toString(),
                evidence.nodeRid(),
                join.profile.displayName,
                join.profile.level,
                join.tags
            )
        )
    }

    override fun onJoinedActor(
        actor: ScenarioActor,
        cancellationToken: CancellationToken
    ) {
        evidence.record("ActorUserJoined", context.spotRid().toString(), actor.actorId() + "#" + actor.nextSequence())
    }

    override fun onLeaveActor(
        actor: ScenarioActor,
        cancellationToken: CancellationToken
    ) {
        evidence.record("ActorUserLeft", context.spotRid().toString(), actor.actorId())
    }

    override fun onDisconnectActor(
        actor: ScenarioActor,
        cancellationToken: CancellationToken
    ) {
        evidence.record("ActorUserDisconnected", context.spotRid().toString(), actor.actorId())
    }

    fun record(marker: String, value: String) {
        evidence.record(marker, context.spotRid().toString(), value)
    }

    fun spotRid(): String = context.spotRid().toString()

    fun nodeRid(): String = evidence.nodeRid()
}
