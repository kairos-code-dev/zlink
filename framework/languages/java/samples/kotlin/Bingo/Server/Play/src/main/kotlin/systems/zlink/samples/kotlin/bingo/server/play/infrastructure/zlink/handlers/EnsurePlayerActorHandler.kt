package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRefSnapshot
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRouteRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes

@ZLinkHandlerGroup("play-route")
class EnsurePlayerActorHandler(
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingRouteRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
    override suspend fun handle(
        request: EnsurePlayerActorReq,
        context: ZLinkRouteRequestContext,
    ) = run {
        if (request.preferredActorNodeRid.isNotBlank() &&
            request.preferredActorNodeRid != SampleTopology.selectedPlayNodeRid()
        ) {
            throw IllegalStateException("EnsurePlayerActor reached the wrong Play node.")
        }
        val actor = actors.getOrCreate(request.actorId, SampleNames.PlayerActorType, request).await()
        EnsurePlayerActorRes(
            request.actorId,
            SampleNames.PlayerActorType,
            ZLinkActorRefSnapshot.from(actor),
        )
    }
}
