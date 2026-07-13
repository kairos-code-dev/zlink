package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.kotlin.ZLinkSuspendingTypedSessionPacketHandler
import kotlinx.coroutines.future.await

class ActorAuthHandler(
    private val actors: ZLinkActorManager,
    private val evidence: ScenarioState
) : ZLinkSuspendingTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ActorAuthReq> {
    override fun packetName(): String = "ActorAuthReq"

    override fun messageType(): Class<Contracts.ActorAuthReq> = Contracts.ActorAuthReq::class.java

    override suspend fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        request: Contracts.ActorAuthReq
    ) {
        val actor = actors.getOrCreate(request.actorId, "scenario", request)
            .await()
        val bound = context.actors().bind(actor)
            .await()
        evidence.record("ActorSessionBound", "session", request.actorId)
        context.client()
            .reply(
                Contracts.ActorAuthRes(
                    bound.actorId(),
                    bound.ref().nodeRid().toString(),
                    context.actors().bound().size,
                    request.profile.displayName,
                    request.profile.level,
                    request.profile.tags
                )
            )
            .submit()
    }
}
