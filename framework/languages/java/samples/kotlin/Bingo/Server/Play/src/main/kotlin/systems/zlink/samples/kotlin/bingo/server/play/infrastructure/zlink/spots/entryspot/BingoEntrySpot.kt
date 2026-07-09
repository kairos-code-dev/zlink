package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkAwait.await
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.ObserveBingoEventsReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.ObserveBingoEventsRes

class BingoEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) : ZLinkEntrySpot<PlayerActor> {
    override fun context(): ZLinkEntrySpotContext = context

    override fun onCreateActor(
        actor: PlayerActor,
        createRequest: ZLinkMessage,
        cancellationToken: CancellationToken,
    ) {
        val request = createRequest.decode(EnsurePlayerActorReq::class.java)
        actor.setDisplayName(request.displayName)
    }

    override fun onActorJoin(
        actor: PlayerActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept()

    override fun onJoinedActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor.destroyAfterEntrySpotJoin) {
            await(context.destroyActor(actor))
        }
    }

    override fun onLeaveActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onDisconnectActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        actor.markDisconnected()
    }

    suspend fun observeEvents(
        actor: PlayerActor,
        request: ObserveBingoEventsReq,
    ): ObserveBingoEventsRes {
        val observerRid = "observe:${request.roomId}:${context.nodeRid()}"
        val settings = BingoRoomSettings.createObserver(
            request.roomId,
            context.nodeRid().toString(),
            SampleTimings.DrawPeriod.toMillis(),
        )
        spots.getOrCreate(BingoRoomSpot::class.java, RoutingId.from(observerRid), ZLinkMessage.of(settings)).await()
        val joined = actor.context().joinSpot(
            RoutingId.from(observerRid),
            BingoRoomJoinReq(
                request.roomId,
                actor.actorId(),
                actor.displayName,
                true,
            ),
        ).await(BingoRoomJoinRes::class.java)
        return ObserveBingoEventsRes(
            joined.reply().state.status == "Running",
            joined.actor().nodeRid().toString(),
        )
    }
}
