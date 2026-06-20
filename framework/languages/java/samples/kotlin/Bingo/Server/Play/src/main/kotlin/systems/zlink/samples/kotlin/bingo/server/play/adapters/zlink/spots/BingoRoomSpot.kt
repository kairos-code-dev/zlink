package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots

import com.fasterxml.jackson.databind.ObjectMapper
import java.time.Duration
import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkTimer
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.notifications.BingoNotificationPublisher
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.BingoRoomSpotCreatedHandler
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.BingoRoomTimerHandler
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.BingoWinnerEventHandler
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoGame
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomGame
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomState
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRewardAnnouncedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinnerEvent
import systems.zlink.samples.kotlin.bingo.shared.contracts.StopObservingBingoEventsReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.StopObservingBingoEventsRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardRes

class BingoRoomSpot(
    private val context: ZLinkSpotContext,
    private val notifications: BingoNotificationPublisher,
    private val createdHandler: BingoRoomSpotCreatedHandler,
    private val json: ObjectMapper,) : ZLinkSuspendingSpot<PlayerActor>() {
    private val actors = mutableMapOf<String, PlayerActor>()
    private val observers = mutableMapOf<String, PlayerActor>()
    private var settings = BingoRoomSettings.create("two-player", 0)
    private var game: BingoRoomGame? = BingoGame.room(context.spotRid().toString(), settings)
    private var timer: ZLinkTimer? = null
    private var cleanupStarted = false

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: Message): ZLinkSpotCreateResponse {
        createdHandler.handle(this, request)
        return ZLinkSpotCreateResponse.accept()
    }

    override fun configure() {
        context.handlers().addSubscribe(
            SampleNames.WinnerTopic,
            BingoWinnerEventHandler::class.java,
        )
    }

    override suspend fun onActorJoinSuspending(
        actor: PlayerActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        val joinRequest = json.readValue(request.toByteArray(), BingoRoomJoinReq::class.java)
        val reply = join(actor, joinRequest)
        return ZLinkSpotActorJoinResponse.accept(Message.from(json.writeValueAsBytes(reply)))
    }

    override fun onJoinedActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
    }

    override fun onLeaveActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        println(
            "bingo room: actor left. room=${context.spotRid()}, actor=${actor.actorId()}, " +
                "observer=${settings.isObserver}, nodeRid=${context.nodeRid()}",
        )
        actors.remove(actor.actorId())
        observers.remove(actor.actorId())
    }

    override fun onDisconnectActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        actor.markDisconnected()
    }

    override suspend fun onInitializeSuspending() {
        if (settings.isObserver) {
            return
        }
        timer = context.addTimer(
            "bingo-draw",
            Duration.ofMillis(settings.drawPeriodMillis),
            BingoRoomTimerHandler::class.java,
            ZLinkTimerOptions(),
        ).await()
    }

    override suspend fun onClosingSuspending() {
        timer?.cancelAsync()?.await()
    }

    suspend fun join(
        actor: PlayerActor,
        request: BingoRoomJoinReq,
    ): BingoRoomJoinRes {
        if (request.actorId != actor.actorId()) {
            throw IllegalStateException("Join request actor id does not match bound actor.")
        }
        if (!request.observeOnly && request.roomId != context.spotRid().toString()) {
            throw IllegalStateException("Join request room id does not match bingo room.")
        }
        actor.setDisplayName(request.displayName)
        actor.joinRoom(request.roomId)
        if (request.observeOnly) {
            return joinObserver(actor, request)
        }
        if (settings.isObserver) {
            throw IllegalStateException("Player actor cannot join an observer BingoRoom.")
        }
        val change = requireGame().join(actor.actorId(), request.displayName)
        actors[actor.actorId()] = actor
        println(
            "bingo room: actor joined. room=${context.spotRid()}, actor=${actor.actorId()}, " +
                "count=${change.state.players.size}, status=${change.state.status}, nodeRid=${context.nodeRid()}",
        )
        notifications.publish(
            change.events,
            { actorId -> if (actorId == actor.actorId()) null else actors[actorId] },
        )
        return BingoRoomJoinRes(change.state)
    }

    suspend fun submitCard(
        actor: PlayerActor,
        request: SubmitBingoCardReq,
    ): SubmitBingoCardRes {
        if (request.roomId != context.spotRid().toString()) {
            throw IllegalStateException("Submit request room id does not match bingo room.")
        }
        val change = requireGame().submitCard(actor.actorId(), request.card)
        notifications.publish(change.events, actors::get)
        return SubmitBingoCardRes(change.state)
    }

    suspend fun tick() {
        val game = this.game
        if (game == null || cleanupStarted) {
            return
        }
        val change = game.drawNext()
        notifications.publish(change.events, actors::get)
        publishWinner(change)
        leaveFinishedActors(change)
    }

    suspend fun announceWinner(event: BingoWinnerEvent) {
        if (!settings.isObserver ||
            observers.isEmpty() ||
            event.roomId != settings.observedRoomId
        ) {
            println(
                "bingo reward: ignored. room=${event.roomId}, actor=${event.actorId}, " +
                    "item=${event.itemId}, observer=${settings.isObserver}, " +
                    "observedRoom=${settings.observedRoomId}, nodeRid=${context.nodeRid()}",
            )
            return
        }
        for (observer in observers.values.toList()) {
            println(
                "bingo reward: announcing. room=${event.roomId}, actor=${event.actorId}, " +
                    "item=${event.itemId}, observer=${observer.actorId()}, nodeRid=${context.nodeRid()}",
            )
            observer.context().boundSession()
                .send(
                    BingoRewardAnnouncedNotify(
                        event.roomId,
                        event.actorId,
                        event.drawSeq,
                        event.itemId,
                        event.itemName,
                        event.rarity,
                        context.nodeRid().toString(),
                    ),
                )
                .submit()
                .await()
            println("bingo reward: announce sent. room=${event.roomId}, observer=${observer.actorId()}")
        }
    }

    suspend fun stopObserving(
        actor: PlayerActor,
        request: StopObservingBingoEventsReq,
    ): StopObservingBingoEventsRes {
        if (!settings.isObserver ||
            request.roomId != settings.observedRoomId ||
            !observers.containsKey(actor.actorId())
        ) {
            return StopObservingBingoEventsRes(false, context.nodeRid().toString())
        }
        observers.remove(actor.actorId())
        context.leaveActor(actor).await()
        println(
            "bingo observer room: actor left. observedRoom=${settings.observedRoomId}, " +
                "observer=${actor.actorId()}, nodeRid=${context.nodeRid()}",
        )
        return StopObservingBingoEventsRes(true, context.nodeRid().toString())
    }

    private suspend fun leaveFinishedActors(change: BingoRoomGame.Change) {
        if (cleanupStarted || change.state.status != BingoRoomGame.Finished) {
            return
        }
        cleanupStarted = true
        for (actor in actors.values.toList()) {
            actor.markForDestroyAfterRoomLeave()
            context.leaveActor(actor).await()
        }
    }

    fun applySettings(settings: BingoRoomSettings) {
        check(settings.isObserver || settings.requiredPlayers > 0) { "Bingo room requires at least one player." }
        check(settings.maxDrawNumber > 0) { "Bingo room requires at least one draw number." }
        check(settings.drawPeriodMillis > 0) {
            "Bingo room draw period must be positive."
        }
        this.settings = settings
        game = if (settings.isObserver) null else BingoGame.room(context.spotRid().toString(), settings)
        cleanupStarted = false
    }

    private suspend fun publishWinner(change: BingoRoomGame.Change) {
        val state = change.state
        if (state.status != BingoRoomGame.Finished || state.winners.isEmpty()) {
            return
        }
        val winner = state.winners.first()
        context.outbound()
            .publish(
                SampleNames.WinnerTopic,
                BingoWinnerEvent(
                    state.roomId,
                    winner,
                    state.drawSeq,
                    "rare-golden-dauber",
                    "Golden Dauber",
                    "Legendary",
                ),
            )
            .submit()
            .await()
        println(
            "bingo reward: published. room=${state.roomId}, actor=$winner, " +
                "item=rare-golden-dauber, nodeRid=${context.nodeRid()}",
        )
    }

    private fun joinObserver(
        actor: PlayerActor,
        request: BingoRoomJoinReq,
    ): BingoRoomJoinRes {
        if (!settings.isObserver || request.roomId != settings.observedRoomId) {
            throw IllegalStateException("Observe-only actor can join only its observer BingoRoom.")
        }
        observers[actor.actorId()] = actor
        println(
            "bingo observer room: actor joined. observedRoom=${settings.observedRoomId}, " +
                "observer=${actor.actorId()}, nodeRid=${context.nodeRid()}",
        )
        return BingoRoomJoinRes(
            BingoRoomState(
                request.roomId,
                BingoRoomGame.Running,
                "",
                false,
                0,
                null,
                emptyList(),
                emptyList(),
                emptyList(),
            ),
        )
    }

    private fun requireGame(): BingoRoomGame =
        game ?: throw IllegalStateException("Observer BingoRoom does not own game state.")
}
