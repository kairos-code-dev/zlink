package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

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
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers.BingoRoomSpotCreatedHandler
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers.BingoRoomTimerHandler
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers.StartBingoGameHandler
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomState
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameRes

class BingoRoomSpot(
    private val context: ZLinkSpotContext,
    private val notifications: BingoNotificationPublisher,
    private val createdHandler: BingoRoomSpotCreatedHandler,
    private val json: ObjectMapper,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpot(coroutines) {
    private val sameSequenceWinnerProbe = listOf("player-2", "player-3")
    private val players = mutableListOf<BingoRoomPlayer>()
    private val drawDeck = ArrayDeque<Int>()
    private val drawnNumbers = mutableListOf<Int>()
    private val winners = mutableListOf<String>()
    private var settings = BingoRoomSettings.create("four-player", 0)
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
        context.handlers().addHandler(StartBingoGameHandler::class.java)
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
        val existing = players.firstOrNull { it.actor.actorId() == actor.actorId() }
        if (existing != null) {
            return BingoRoomJoinRes(snapshot())
        }

        if (status != WaitingForPlayers || players.size >= settings.requiredPlayers) {
            throw IllegalStateException("Room ${request.roomId} cannot accept more players.")
        }

        actor.setDisplayName(request.displayName)
        actor.joinRoom(request.roomId)
        val player = BingoRoomPlayer(actor, players.size, BingoCard.create())
        players += player

        val state = snapshot()
        notifications.publish(playerJoinedEvents(player, state))
        return BingoRoomJoinRes(state)
    }

    suspend fun start(
        actor: PlayerActor,
        request: StartBingoGameReq,
    ): StartBingoGameRes {
        if (request.roomId != context.spotRid().toHex()) {
            throw IllegalStateException("Start request room id does not match actor room.")
        }
        if (players.isEmpty() || players.first().actor.actorId() != actor.actorId()) {
            throw IllegalStateException("Only the host actor can start the bingo room.")
        }
        if (players.size != settings.requiredPlayers) {
            throw IllegalStateException("Bingo room requires exactly ${settings.requiredPlayers} players before start.")
        }
        if (status == WaitingForPlayers) {
            status = Running
            val state = snapshot()
            notifications.publish(eventsForAll(BingoRoomEventKind.GAME_STARTED, state))
            return StartBingoGameRes(snapshot())
        }
        return StartBingoGameRes(snapshot())
    }

    suspend fun tick() {
        if (status != Running || drawDeck.isEmpty()) {
            return
        }

        val number = drawDeck.remove()
        drawnNumbers += number
        val newlyCompleted = mutableListOf<String>()
        for (player in players) {
            val before = player.card.completedLines()
            player.card.markDrawnNumber(number)
            if (player.card.completedLines() > before) {
                newlyCompleted += player.actor.actorId()
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
        notifications.publish(numberDrawnEvents(state, number))
        notifications.publish(eventsForAll(kind, state))
    }

    private fun snapshot(): BingoRoomState {
        val hostActorId = players.firstOrNull()?.actor?.actorId() ?: ""
        val lastDrawn = drawnNumbers.lastOrNull()
        return BingoRoomState(
            context.spotRid().toHex(),
            status,
            hostActorId,
            status == WaitingForPlayers && players.size == settings.requiredPlayers,
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
            BingoRoomEvent(
                BingoRoomEventKind.PLAYER_JOINED,
                player.actor,
                state,
                joined.actor.actorId(),
                joined.actor.displayName,
                joined.seat,
                joined.seat == 0,
                0,
            )
        }

    private fun numberDrawnEvents(
        state: BingoRoomState,
        number: Int,
    ): List<BingoRoomEvent> =
        players.map { player ->
            BingoRoomEvent(
                BingoRoomEventKind.NUMBER_DRAWN,
                player.actor,
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
                player.actor,
                state,
                null,
                null,
                -1,
                false,
                0,
            )
        }

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
