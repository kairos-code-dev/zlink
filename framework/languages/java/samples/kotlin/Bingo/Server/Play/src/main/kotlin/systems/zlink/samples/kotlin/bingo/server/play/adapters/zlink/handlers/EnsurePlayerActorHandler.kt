package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.ActorRefSnapshot
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes

@ZLinkHandlerGroup("play")
class EnsurePlayerActorHandler(
    private val actors: ZLinkActorManager,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
    override fun handleAsync(
        request: EnsurePlayerActorReq,
        context: ZLinkRequestContext,
    ) = coroutines.completionStage {
        val actor = actors.getOrCreate(request.actorId, SampleNames.PlayerActorType).await()
        if (actor is PlayerActor) {
            actor.setDisplayName(request.displayName)
        }
        val joined = actor.context()
            .joinEntrySpot(RoutingId.from(SampleTopology.PlayRid))
            .timeout(SampleTimings.RequestTimeout)
            .submit()
            .await()
        EnsurePlayerActorRes(
            request.actorId,
            SampleNames.PlayerActorType,
            toSnapshot(joined),
        )
    }

    private fun toSnapshot(actor: ZLinkActorRef): ActorRefSnapshot =
        ActorRefSnapshot(
            actor.nodeRid().toBytes(),
            actor.actorId(),
            actor.epoch(),
        )
}
