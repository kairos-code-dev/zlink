package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler

class ActorAuthHandler(
    private val actors: ZLinkActorManager,
    private val evidence: ScenarioState
) : ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ActorAuthRequest> {
    override fun packetName(): String = "ActorAuthRequest"

    override fun messageType(): Class<Contracts.ActorAuthRequest> = Contracts.ActorAuthRequest::class.java

    override fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        request: Contracts.ActorAuthRequest
    ) {
        val actor = actors.getOrCreate(request.actorId, "scenario", request)
            .toCompletableFuture()
            .join()
        val bound = context.actors().bind(actor)
            .toCompletableFuture()
            .join()
        evidence.record("ActorSessionBound", "session", request.actorId)
        context.client()
            .reply(
                Contracts.ActorAuthReply(
                    bound.actorId(),
                    bound.ref().nodeRid().toString(),
                    context.actors().bound().size,
                    request.profile.displayName,
                    request.profile.level,
                    request.profile.tags
                )
            )
            .await()
    }
}
