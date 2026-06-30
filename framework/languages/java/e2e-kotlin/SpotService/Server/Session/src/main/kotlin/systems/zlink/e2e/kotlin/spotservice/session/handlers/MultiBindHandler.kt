package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler

class MultiBindHandler(
    private val actors: ZLinkActorManager,
    private val evidence: ScenarioState
) : ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.MultiBindRequest> {
    override fun packetName(): String = "MultiBindRequest"

    override fun messageType(): Class<Contracts.MultiBindRequest> = Contracts.MultiBindRequest::class.java

    override fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        request: Contracts.MultiBindRequest
    ) {
        listOf(request.firstActorId, request.secondActorId).forEach { actorId ->
            val actor = actors.getOrCreate(
                actorId,
                "scenario",
                Contracts.ActorAuthRequest(actorId, request.profile)
            ).toCompletableFuture().join()
            context.actors().bind(actor).toCompletableFuture().join()
            evidence.record("ActorSessionBound", "session", actorId)
        }
        context.client()
            .reply(Contracts.MultiBindReply(context.actors().bound().size))
            .await()
    }
}
