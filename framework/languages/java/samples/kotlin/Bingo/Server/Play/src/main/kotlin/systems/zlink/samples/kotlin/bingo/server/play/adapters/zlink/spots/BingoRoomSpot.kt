package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots

import java.util.ArrayDeque
import java.time.Duration
import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkTimer
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.notifications.BingoNotificationPublisher
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.BingoRoomSpotCreatedHandler
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.BingoRoomTimerHandler
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.SubmitBingoCardHandler
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoCard
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomEvent
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomEventKind
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomPlayer
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomState
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardRes

class BingoRoomSpot(
    private val context: ZLinkSpotContext,
    private val notifications: BingoNotificationPublisher,
    private val createdHandler: BingoRoomSpotCreatedHandler,
    private val json: ObjectMapper,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpot(coroutines) {
    private val players = mutableListOf<BingoRoomPlayer>()
    private val actors = mutableMapOf<String, PlayerActor>()
    private val drawDeck = ArrayDeque<Int>()
    private val drawnNumbers = mutableListOf<Int>()
    private val winners = mutableListOf<String>()
    private var settings = BingoRoomSettings.create("two-player", 0)
    private var timer: ZLinkTimer? = null
    private var status: String = WaitingForPlayers

    init {
        resetDrawDeck()
    }

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreate(request: Message): ZLinkSpotCreateResponse {
        createdHandler.handle(this, request)
        return ZLinkSpotCreateResponse.accept()
    }

    override fun configure() {
        context.handlers().addHandler(SubmitBingoCardHandler::class.java)
    }

    override suspend fun onActorJoin(
        actor: ZLinkActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        require(actor is PlayerActor) { "Bingo room only accepts PlayerActor." }
        val joinRequest = json.readValue(request.toByteArray(), BingoRoomJoinReq::class.java)
        val reply = join(actor, joinRequest)
        return ZLinkSpotActorJoinResponse.accept(Message.from(json.writeValueAsBytes(reply)))
    }

    override suspend fun onPostActorJoined(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) {
    }

    override suspend fun onActorLeft(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) {
    }

    override suspend fun onInitialize() {
        timer = context.addTimer(
            "bingo-draw",
            Duration.ofMillis(settings.drawPeriodMillis),
            BingoRoomTimerHandler::class.java,
            ZLinkTimerOptions(),
        ).await()
    }

    override suspend fun onClosing() {
        timer?.cancelAsync()?.await()
    }

    suspend fun join(
        actor: PlayerActor,
        request: BingoRoomJoinReq,
    ): BingoRoomJoinRes {
        if (request.actorId != actor.actorId()) {
            throw IllegalStateException("Join request actor id does not match bound actor.")
        }
        if (request.roomId != context.spotRid().toString()) {
            throw IllegalStateException("Join request room id does not match bingo room.")
        }
        val existing = players.firstOrNull { it.actorId == actor.actorId() }
        if (existing != null) {
            return BingoRoomJoinRes(snapshot())
        }

        if (status != WaitingForPlayers || players.size >= settings.requiredPlayers) {
            throw IllegalStateException("Room ${request.roomId} cannot accept more players.")
        }

        actor.setDisplayName(request.displayName)
        actor.joinRoom(request.roomId)
        actors[actor.actorId()] = actor
        val player = BingoRoomPlayer(actor.actorId(), actor.displayName, players.size, null)
        players += player

        val state = snapshot()
        notifications.publish(playerJoinedEvents(player, state), actors::get)
        if (players.size == settings.requiredPlayers) {
            status = Running
            val started = snapshot()
            notifications.publish(eventsForAll(BingoRoomEventKind.GAME_STARTED, started), actors::get)
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
        check(status == Running) { "bingo card can be submitted only after the room starts" }
        val player = players.firstOrNull { it.actorId == actor.actorId() }
            ?: throw IllegalStateException("player has not joined the bingo room")
        check(player.card == null) { "bingo card was already submitted" }
        player.card = BingoCard.from(request.card)
        return SubmitBingoCardRes(snapshot())
    }

    suspend fun tick() {
        if (status != Running || !allCardsSubmitted() || drawDeck.isEmpty()) {
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

    private fun allCardsSubmitted(): Boolean =
        players.size == settings.requiredPlayers && players.all { it.card != null }

    private fun resetDrawDeck() {
        drawDeck.clear()
        for (number in 1..settings.maxDrawNumber) {
            drawDeck.add(number)
        }
    }

    fun applySettings(settings: BingoRoomSettings) {
        check(settings.requiredPlayers > 0) { "Bingo room requires at least one player." }
        check(settings.maxDrawNumber > 0) { "Bingo room requires at least one draw number." }
        check(settings.drawPeriodMillis > 0) {
            "Bingo room draw period must be positive."
        }
        this.settings = settings
        resetDrawDeck()
    }

    companion object {
        private const val WaitingForPlayers = "WaitingForPlayers"
        private const val Running = "Running"
        private const val Finished = "Finished"
    }
}
