package systems.zlink.e2e.kotlin.spotservice.session.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import kotlinx.coroutines.future.await
import systems.zlink.framework.kotlin.ZLinkSuspendingTypedSessionPacketHandler

class MultiBindHandler(
    private val actors: ZLinkActorManager,
    private val evidence: ScenarioState
) : ZLinkSuspendingTypedSessionPacketHandler<ZLinkSessionContext, Contracts.MultiBindReq> {
    override fun packetName(): String = "MultiBindReq"

    override fun messageType(): Class<Contracts.MultiBindReq> = Contracts.MultiBindReq::class.java

    override suspend fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        request: Contracts.MultiBindReq
    ) {
        listOf(request.firstActorId, request.secondActorId).forEach { actorId ->
            val actor = actors.getOrCreate(
                actorId,
                "scenario",
                Contracts.ActorAuthReq(actorId, request.profile)
            ).await()
            context.actors().bind(actor).await()
            evidence.record("ActorSessionBound", "session", actorId)
        }
        context.client()
            .reply(Contracts.MultiBindRes(context.actors().bound().size))
            .submit()
    }
}
