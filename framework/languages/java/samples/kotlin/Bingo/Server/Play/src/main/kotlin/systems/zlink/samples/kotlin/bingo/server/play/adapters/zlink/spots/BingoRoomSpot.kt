package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots

import java.util.ArrayDeque
import java.time.Duration
import com.fasterxml.jackson.databind.ObjectMapper
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
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoCard
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomEvent
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomEventKind
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomPlayer
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
    private val players = mutableListOf<BingoRoomPlayer>()
    private val actors = mutableMapOf<String, PlayerActor>()
    private val observers = mutableMapOf<String, PlayerActor>()
    private val drawDeck = ArrayDeque<Int>()
    private val drawnNumbers = mutableListOf<Int>()
    private val winners = mutableListOf<String>()
    private var settings = BingoRoomSettings.create("two-player", 0)
    private var timer: ZLinkTimer? = null
    private var status: String = WaitingForPlayers
    private var cleanupStarted = false

    init {
        resetDrawDeck()
    }

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
        val existing = players.firstOrNull { it.actorId == actor.actorId() }
        if (existing != null) {
            return BingoRoomJoinRes(snapshot())
        }

        if (status != WaitingForPlayers || players.size >= settings.requiredPlayers) {
            throw IllegalStateException("Room ${request.roomId} cannot accept more players.")
        }

        actors[actor.actorId()] = actor
        val player = BingoRoomPlayer(actor.actorId(), actor.displayName, players.size, null)
        players += player

        val state = snapshot()
        println(
            "bingo room: actor joined. room=${context.spotRid()}, actor=${actor.actorId()}, " +
                "count=${state.players.size}, status=${state.status}, nodeRid=${context.nodeRid()}",
        )
        notifications.publish(playerJoinedEvents(player, state), actors::get)
        if (players.size == settings.requiredPlayers) {
            status = Running
            val started = snapshot()
            notifications.publish(
                eventsForAllExcept(BingoRoomEventKind.GAME_STARTED, started, actor.actorId()),
                actors::get,
            )
            return BingoRoomJoinRes(started)
        }
        return BingoRoomJoinRes(state)
    }

    suspend fun submitCard(
        actor: PlayerActor,
        request: SubmitBingoCardReq,
    ): SubmitBingoCardRes {
        if (request.roomId != context.spotRid().toString()) {
            throw IllegalStateException("Submit request room id does not match bingo room.")
        }
        if (settings.isObserver) {
            throw IllegalStateException("Observer BingoRoom does not own game state.")
        }
        check(status == Running) { "bingo card can be submitted only after the room starts" }
        val player = players.firstOrNull { it.actorId == actor.actorId() }
            ?: throw IllegalStateException("player has not joined the bingo room")
        check(player.card == null) { "bingo card was already submitted" }
        player.card = BingoCard.from(request.card)
        return SubmitBingoCardRes(snapshot())
    }

    suspend fun tick() {
        if (settings.isObserver || cleanupStarted || status != Running || !allCardsSubmitted() || drawDeck.isEmpty()) {
            return
        }

        val number = drawDeck.remove()
        drawnNumbers += number
        val newlyCompleted = mutableListOf<String>()
        for (player in players) {
            val card = player.card ?: continue
            val before = card.completedLines()
            card.markDrawnNumber(number)
            if (card.completedLines() > before) {
                newlyCompleted += player.actorId
            }
        }

        if (newlyCompleted.isNotEmpty()) {
            status = Finished
            winners += newlyCompleted
        }

        val state = snapshot()
        val kind = if (status == Finished) {
            BingoRoomEventKind.GAME_ENDED
        } else {
            BingoRoomEventKind.STATE
        }
        notifications.publish(numberDrawnEvents(state, number), actors::get)
        notifications.publish(eventsForAll(kind, state), actors::get)
        publishWinner(state)
        leaveFinishedActors()
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

    private suspend fun leaveFinishedActors() {
        if (cleanupStarted || status != Finished) {
            return
        }
        cleanupStarted = true
        for (actor in actors.values.toList()) {
            actor.markForDestroyAfterRoomLeave()
            context.leaveActor(actor).await()
        }
    }

    private fun snapshot(): BingoRoomState {
        val hostActorId = players.firstOrNull()?.actorId ?: ""
        val lastDrawn = drawnNumbers.lastOrNull()
        return BingoRoomState(
            context.spotRid().toString(),
            status,
            hostActorId,
            false,
            drawnNumbers.size,
            lastDrawn,
            drawnNumbers.toList(),
            players.map { it.toState(hostActorId) },
            winners.toList(),
        )
    }

    private fun playerJoinedEvents(
        joined: BingoRoomPlayer,
        state: BingoRoomState,
    ): List<BingoRoomEvent> =
        players.map { player ->
            if (player.actorId == joined.actorId) {
                return@map null
            }
            BingoRoomEvent(
                BingoRoomEventKind.PLAYER_JOINED,
                player.actorId,
                state,
                joined.actorId,
                joined.displayName,
                joined.seat,
                joined.seat == 0,
                0,
            )
        }.filterNotNull()

    private fun numberDrawnEvents(
        state: BingoRoomState,
        number: Int,
    ): List<BingoRoomEvent> =
        players.map { player ->
            BingoRoomEvent(
                BingoRoomEventKind.NUMBER_DRAWN,
                player.actorId,
                state,
                null,
                null,
                -1,
                false,
                number,
            )
        }

    private fun eventsForAll(
        kind: BingoRoomEventKind,
        state: BingoRoomState,
    ): List<BingoRoomEvent> =
        players.map { player ->
            BingoRoomEvent(
                kind,
                player.actorId,
                state,
                null,
                null,
                -1,
                false,
                0,
            )
        }

    private fun eventsForAllExcept(
        kind: BingoRoomEventKind,
        state: BingoRoomState,
        excludedActorId: String,
    ): List<BingoRoomEvent> =
        eventsForAll(kind, state)
            .filter { event -> event.recipientActorId != excludedActorId }

    private fun allCardsSubmitted(): Boolean =
        players.size == settings.requiredPlayers && players.all { it.card != null }

    private fun resetDrawDeck() {
        drawDeck.clear()
        for (number in 1..settings.maxDrawNumber) {
            drawDeck.add(number)
        }
    }

    fun applySettings(settings: BingoRoomSettings) {
        check(settings.isObserver || settings.requiredPlayers > 0) { "Bingo room requires at least one player." }
        check(settings.maxDrawNumber > 0) { "Bingo room requires at least one draw number." }
        check(settings.drawPeriodMillis > 0) {
            "Bingo room draw period must be positive."
        }
        this.settings = settings
        cleanupStarted = false
        resetDrawDeck()
    }

    private suspend fun publishWinner(state: BingoRoomState) {
        if (state.status != Finished || state.winners.isEmpty()) {
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
                Running,
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

    companion object {
        private const val WaitingForPlayers = "WaitingForPlayers"
        private const val Running = "Running"
        private const val Finished = "Finished"
    }
}
