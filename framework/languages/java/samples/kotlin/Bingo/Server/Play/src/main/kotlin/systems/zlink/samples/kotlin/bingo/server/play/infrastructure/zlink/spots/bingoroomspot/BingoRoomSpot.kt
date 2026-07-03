package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot

import com.fasterxml.jackson.databind.ObjectMapper
import java.time.Duration
import kotlinx.coroutines.future.await
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkTimer
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.BingoRoomSpotCreatedHandler
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.BingoRoomTimerHandler
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.BingoWinnerMsgHandler
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.StopObservingBingoEventsHandler
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.SubmitBingoCardHandler
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoGame
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomEvent
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomEventKind
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomGame
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameEndedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoGameStartedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoNumberDrawnNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomState
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRewardAnnouncedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoStateNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinnerMsg
import systems.zlink.samples.kotlin.bingo.shared.contracts.PlayerJoinedNotify
import systems.zlink.samples.kotlin.bingo.shared.contracts.StopObservingBingoEventsReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.StopObservingBingoEventsRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardRes

class BingoRoomSpot(
    private val context: ZLinkSpotContext,
    private val createdHandler: BingoRoomSpotCreatedHandler,
    private val json: ObjectMapper,) : ZLinkSuspendingSpot<PlayerActor>() {
    private val actors = mutableMapOf<String, PlayerActor>()
    private val observers = mutableMapOf<String, PlayerActor>()
    private var settings = BingoRoomSettings.create(
        "two-player",
        0,
        SampleTimings.DrawPeriod.toMillis(),
    )
    private var game: BingoRoomGame? = BingoGame.room(context.spotRid().toString(), settings)
    private var timer: ZLinkTimer? = null
    private var cleanupStarted = false

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: ZLinkMessage): ZLinkSpotCreateResponse {
        createdHandler.handle(this, request)
        return ZLinkSpotCreateResponse.accept()
    }

    override fun configure() {
        context.handlers().addHandler(SubmitBingoCardHandler::class.java)
        context.handlers().addHandler(StopObservingBingoEventsHandler::class.java)
        context.handlers().addSubscribe(
            SampleNames.WinnerTopic,
            BingoWinnerMsgHandler::class.java,
        )
    }

    override suspend fun onActorJoinSuspending(
        actor: PlayerActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        val joinRequest = request.decode(BingoRoomJoinReq::class.java)
        val reply = join(actor, joinRequest)
        return ZLinkSpotActorJoinResponse.accept(reply)
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
        if (settings.observerMode()) {
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
        if (settings.observerMode()) {
            throw IllegalStateException("Player actor cannot join an observer BingoRoom.")
        }
        val change = requireGame().join(actor.actorId(), request.displayName)
        actors[actor.actorId()] = actor
        publishEvents(
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
        publishEvents(change.events, actors::get)
        return SubmitBingoCardRes(change.state)
    }

    suspend fun tick() {
        val game = this.game
        if (game == null || cleanupStarted) {
            return
        }
        val change = game.drawNext()
        publishEvents(change.events, actors::get)
        publishWinner(change)
        leaveFinishedActors(change)
    }

    suspend fun announceWinner(event: BingoWinnerMsg) {
        if (!settings.observerMode() ||
            observers.isEmpty() ||
            event.roomId != settings.observedRoomId
        ) {
            return
        }
        for (observer in observers.values.toList()) {
            observer.push(
                BingoRewardAnnouncedNotify(
                    event.roomId,
                    event.actorId,
                    event.drawSeq,
                    event.itemId,
                    event.itemName,
                    event.rarity,
                    context.nodeRid().toString(),
                )
            )
        }
    }

    suspend fun stopObserving(
        actor: PlayerActor,
        request: StopObservingBingoEventsReq,
    ): StopObservingBingoEventsRes {
        if (!settings.observerMode() ||
            request.roomId != settings.observedRoomId ||
            !observers.containsKey(actor.actorId())
        ) {
            return StopObservingBingoEventsRes(false, context.nodeRid().toString())
        }
        observers.remove(actor.actorId())
        context.leaveActor(actor).exceptionally { null }
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
        check(settings.observerMode() || settings.requiredPlayers > 0) { "Bingo room requires at least one player." }
        check(settings.maxDrawNumber > 0) { "Bingo room requires at least one draw number." }
        check(settings.drawPeriodMillis > 0) {
            "Bingo room draw period must be positive."
        }
        this.settings = settings
        game = if (settings.observerMode()) null else BingoGame.room(context.spotRid().toString(), settings)
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
                BingoWinnerMsg(
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
    }

    private suspend fun publishEvents(
        events: List<BingoRoomEvent>,
        actorResolver: (String) -> PlayerActor?,
    ) {
        for (event in events) {
            publishEvent(event, actorResolver(event.recipientActorId))
        }
    }

    private suspend fun publishEvent(
        event: BingoRoomEvent,
        recipient: PlayerActor?,
    ) {
        if (recipient == null) {
            return
        }
        when (event.kind) {
            BingoRoomEventKind.PLAYER_JOINED ->
                recipient.push(
                    PlayerJoinedNotify(
                        event.state.roomId,
                        event.joinedActorId!!,
                        event.joinedDisplayName!!,
                        event.seat,
                        event.host,
                        event.state,
                    )
                )

            BingoRoomEventKind.GAME_STARTED ->
                recipient.push(BingoGameStartedNotify(event.state))

            BingoRoomEventKind.NUMBER_DRAWN ->
                recipient.push(
                    BingoNumberDrawnNotify(
                        event.state.roomId,
                        event.state.drawSeq,
                        event.drawnNumber,
                        event.state,
                    )
                )

            BingoRoomEventKind.STATE ->
                recipient.push(BingoStateNotify(event.state))

            BingoRoomEventKind.GAME_ENDED ->
                recipient.push(BingoGameEndedNotify(event.state))
        }
    }

    private fun joinObserver(
        actor: PlayerActor,
        request: BingoRoomJoinReq,
    ): BingoRoomJoinRes {
        if (!settings.observerMode() || request.roomId != settings.observedRoomId) {
            throw IllegalStateException("Observe-only actor can join only its observer BingoRoom.")
        }
        observers[actor.actorId()] = actor
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
