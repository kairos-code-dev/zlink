package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpot
import systems.zlink.framework.kotlin.awaitJoin
import systems.zlink.framework.messaging.ZLinkMessage
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
) : ZLinkSuspendingEntrySpot<PlayerActor>() {
    override fun context(): ZLinkEntrySpotContext = context

    override suspend fun onCreateActorSuspending(
        actor: PlayerActor,
        createRequest: ZLinkMessage,
    ) {
        val request = createRequest.decode(EnsurePlayerActorReq::class.java)
        actor.setDisplayName(request.displayName)
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept()

    override suspend fun onJoinedActorSuspending(actor: PlayerActor) {
        if (actor.destroyAfterEntrySpotJoin) {
            context.destroyActor(actor).await()
        }
    }

    override suspend fun onLeaveActorSuspending(actor: PlayerActor) = Unit

    override suspend fun onDisconnectActorSuspending(actor: PlayerActor) {
        actor.markDisconnected()
    }

    suspend fun observeEvents(
        actor: PlayerActor,
        request: ObserveBingoEventsReq,
    ): ObserveBingoEventsRes {
        val observerSpotId = "observe:${request.roomId}:${actor.actorId()}"
        val settings = BingoRoomSettings.createObserver(
            request.roomId,
            actor.actorId(),
            SampleTimings.DrawPeriod.toMillis(),
        )
        spots.getOrCreate(observerSpotId, BingoRoomSpot::class.java.name)
            .request(settings)
            .submit()
            .await()
        val joined = actor.context().joinSpot(
            observerSpotId,
            BingoRoomJoinReq(
                request.roomId,
                actor.actorId(),
                actor.displayName,
                true,
            ),
        ).awaitJoin(BingoRoomJoinRes::class.java)
        val accepted = joined as? systems.zlink.framework.actors.ZLinkActorJoinResult.Accepted
            ?: error("observer actor join was rejected")
        return ObserveBingoEventsRes(
            accepted.reply().state.status == "Running",
        )
    }
}
