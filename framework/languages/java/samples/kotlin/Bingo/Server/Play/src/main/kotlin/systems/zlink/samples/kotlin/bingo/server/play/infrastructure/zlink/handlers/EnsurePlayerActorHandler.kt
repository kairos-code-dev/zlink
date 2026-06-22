package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRouteRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.ActorRefSnapshot
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
        val actor = actors.getOrCreate(request.actorId, SampleNames.PlayerActorType).await()
        if (actor is PlayerActor) {
            actor.setDisplayName(request.displayName)
        }
        val joined = actor.context()
            .joinEntrySpot(RoutingId.from(SampleTopology.selectedPlayNodeRid()), Message.from(ByteArray(0)))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Message::class.java)
            .await()
        EnsurePlayerActorRes(
            request.actorId,
            SampleNames.PlayerActorType,
            toSnapshot(joined.actor()),
        )
    }

    private fun toSnapshot(actor: ZLinkActorRef): ActorRefSnapshot =
        ActorRefSnapshot(
            actor.nodeRid().toBytes(),
            actor.actorId(),
            actor.epoch(),
        )
}
