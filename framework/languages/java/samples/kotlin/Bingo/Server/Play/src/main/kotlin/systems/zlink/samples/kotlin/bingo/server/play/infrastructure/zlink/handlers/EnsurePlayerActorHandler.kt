package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.ActorRefWire
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes

class EnsurePlayerActorHandler(
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingSpotRequestHandler<BingoEntrySpot, EnsurePlayerActorReq, EnsurePlayerActorRes> {
    override suspend fun handle(
        spot: BingoEntrySpot,
        request: EnsurePlayerActorReq,
    ) = run {
        val actor = actors.getOrCreate(request.actorId, SampleNames.PlayerActorType, request).await()
        EnsurePlayerActorRes(
            request.actorId,
            SampleNames.PlayerActorType,
            ActorRefWire(
                actor.nodeRid().toString(),
                actor.actorId(),
                actor.generation(),
            ),
        )
    }
}
