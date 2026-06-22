package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.ObserveBingoEventsReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.ObserveBingoEventsRes

class BingoEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) : ZLinkEntrySpot<PlayerActor> {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {}

    override fun onCreateActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onActorJoin(
        actor: PlayerActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept(Message.from(ByteArray(0)))

    override fun onJoinedActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor.destroyAfterEntrySpotJoin) {
            context.destroyActor(actor).toCompletableFuture().join()
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
        val settingsPart = Message.from(json.writeValueAsBytes(settings))
        try {
            spots.getOrCreate(BingoRoomSpot::class.java, RoutingId.from(observerRid), settingsPart).await()
        } finally {
            settingsPart.close()
        }
        val joined = actor.context()
            .joinSpot(
                RoutingId.from(observerRid),
                BingoRoomJoinReq(
                    request.roomId,
                    actor.actorId(),
                    actor.displayName,
                    true,
                ),
            )
            .submit(BingoRoomJoinRes::class.java)
            .await()
        return ObserveBingoEventsRes(
            joined.reply().state.status == "Running",
            joined.actor().nodeRid().toString(),
        )
    }
}
