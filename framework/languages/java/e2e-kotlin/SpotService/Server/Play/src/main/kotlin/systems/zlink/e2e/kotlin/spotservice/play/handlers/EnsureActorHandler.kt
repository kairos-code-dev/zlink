package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRouteRequestHandler
import systems.zlink.framework.kotlin.await

class EnsureActorHandler(
    private val actors: ZLinkActorManager,
    private val state: ScenarioState,
) : ZLinkSuspendingRouteRequestHandler<Contracts.EnsureActorReq, Contracts.EnsureActorRes> {
    override suspend fun handle(
        request: Contracts.EnsureActorReq,
        context: ZLinkRouteRequestContext,
    ): Contracts.EnsureActorRes {
        val actor = actors.getOrCreate(request.actorId, "scenario")
            .await()
        state.record("EnsureActor", "entry", request.actorId)
        return Contracts.EnsureActorRes(
            actor.actorId(),
            actor.nodeRid().toString(),
            actor.generation()
        )
    }
}
