package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

import java.util.ArrayDeque
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkTimer
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers.BingoRoomActorJoinedHandler
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers.BingoRoomActorLeftHandler
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers.BingoRoomJoinHandler
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers.BingoRoomTimerHandler
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers.StartBingoGameHandler
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomState
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameRes

class BingoRoomSpot(
    private val context: ZLinkSpotContext,
    private val notifications: BingoNotificationPublisher,
) : ZLinkSpot {
    private val sameSequenceWinnerProbe = listOf("player-2", "player-3")
    private val players = mutableListOf<BingoRoomPlayer>()
    private val drawDeck = ArrayDeque<Int>()
    private val drawnNumbers = mutableListOf<Int>()
    private val winners = mutableListOf<String>()
    private var timer: ZLinkTimer? = null
    private var status: String = WaitingForPlayers

    init {
        resetDrawDeck()
    }

    override fun context(): ZLinkSpotContext = context

    override fun configure() {
        context.handlers().addHandler(BingoRoomJoinHandler::class.java)
        context.handlers().addHandler(StartBingoGameHandler::class.java)
        context.handlers().addHandler(BingoRoomActorJoinedHandler::class.java)
        context.handlers().addHandler(BingoRoomActorLeftHandler::class.java)
    }

    override fun onInitializeAsync(): CompletionStage<Void> =
        context.addTimer(
            "bingo-draw",
            SampleTimings.DrawPeriod,
            BingoRoomTimerHandler::class.java,
            ZLinkTimerOptions(),
        )
            .thenAccept { timer = it }

    override fun onClosingAsync(): CompletionStage<Void> =
        timer?.cancelAsync() ?: CompletableFuture.completedFuture(null)

    fun joinAsync(
        actor: PlayerActor,
        request: BingoRoomJoinReq,
    ): CompletionStage<BingoRoomJoinRes> {
        val existing = players.firstOrNull { it.actor.actorId() == actor.actorId() }
        if (existing != null) {
            return CompletableFuture.completedFuture(BingoRoomJoinRes(snapshot()))
        }

        if (status != WaitingForPlayers || players.size >= RequiredPlayers) {
            return CompletableFuture.failedFuture(
                IllegalStateException("Room ${request.roomId} cannot accept more players."),
            )
        }

        actor.setDisplayName(request.displayName)
        actor.joinRoom(request.roomId)
        val player = BingoRoomPlayer(actor, players.size, BingoCard.create())
        players += player

        val state = snapshot()
        return notifications.publishAsync(playerJoinedEvents(player, state))
            .thenApply { BingoRoomJoinRes(state) }
    }

    fun startAsync(
        actor: PlayerActor,
        request: StartBingoGameReq,
    ): CompletionStage<StartBingoGameRes> {
        if (request.roomId != context.spotRid().toHex()) {
            return CompletableFuture.failedFuture(
                IllegalStateException("Start request room id does not match actor room."),
            )
        }
        if (players.isEmpty() || players.first().actor.actorId() != actor.actorId()) {
            return CompletableFuture.failedFuture(
                IllegalStateException("Only the host actor can start the bingo room."),
            )
        }
        if (players.size != RequiredPlayers) {
            return CompletableFuture.failedFuture(
                IllegalStateException("Bingo room requires exactly $RequiredPlayers players before start."),
            )
        }
        if (status == WaitingForPlayers) {
            status = Running
            val state = snapshot()
            return notifications.publishAsync(eventsForAll(BingoRoomEventKind.GAME_STARTED, state))
                .thenApply { StartBingoGameRes(snapshot()) }
        }
        return CompletableFuture.completedFuture(StartBingoGameRes(snapshot()))
    }

    fun tickAsync(): CompletionStage<Void> {
        if (status != Running || drawDeck.isEmpty()) {
            return CompletableFuture.completedFuture(null)
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
        return notifications.publishAsync(numberDrawnEvents(state, number))
            .thenCompose { notifications.publishAsync(eventsForAll(kind, state)) }
    }

    private fun snapshot(): BingoRoomState {
        val hostActorId = players.firstOrNull()?.actor?.actorId() ?: ""
        val lastDrawn = drawnNumbers.lastOrNull()
        return BingoRoomState(
            context.spotRid().toHex(),
            status,
            hostActorId,
            status == WaitingForPlayers && players.size == RequiredPlayers,
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
        for (number in 1..MaxDrawNumber) {
            drawDeck.add(number)
        }
    }

    companion object {
        private const val WaitingForPlayers = "WaitingForPlayers"
        private const val Running = "Running"
        private const val Finished = "Finished"
        private const val RequiredPlayers = 4
        private const val MaxDrawNumber = 75
    }
}
