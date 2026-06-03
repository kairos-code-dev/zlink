package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots

import java.time.Duration
import java.time.Instant
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkTimer
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers.TicTacToeGameCreatedHandler
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers.TicTacToeGameTimerHandler
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameState
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameStateNotify
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerJoinedNotify
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinRes

class TicTacToeGame(
    private val context: ZLinkSpotContext,
) : ZLinkSpot {
    private val gameTickPeriod: Duration = Duration.ofSeconds(1)
    private val turnTimeout: Duration = Duration.ofSeconds(15)
    val gameId: String = context.spotRid().toHex()
    private val board = ".........".toCharArray()
    private val players = mutableListOf<PlayerSlot>()
    private var status = "WaitingForPlayers"
    private var nextTurn = "X"
    private var winnerValue: String? = null
    private var lastMoveActorId: String? = null
    private var lastMoveCell: Int? = null
    private var turnDeadline: Instant? = null
    private var gameTick: ZLinkTimer? = null
    private var created = false

    override fun context(): ZLinkSpotContext = context
    override fun onCreateAsync(createParts: MutableList<Message>): CompletionStage<Void> =
        TicTacToeGameCreatedHandler().handleAsync(this, createParts)

    override fun onInitializeAsync(): CompletionStage<Void> =
        context.addTimer(
            "game-tick",
            gameTickPeriod,
            TicTacToeGameTimerHandler::class.java,
            ZLinkTimerOptions(),
        ).thenAccept { timer -> gameTick = timer }

    override fun onClosingAsync(): CompletionStage<Void> =
        gameTick?.cancelAsync() ?: CompletableFuture.completedFuture(null)

    fun markCreated(createParts: List<Message>) {
        require(createParts.isEmpty()) { "tic-tac-toe game creation does not accept payload parts" }
        created = true
    }

    fun join(actor: PlayActor, gameId: String): CompletionStage<TicTacToeGameJoinRes> {
        ensureCreated()
        var slot = players.firstOrNull { it.actor.actorId == actor.actorId }
        val isNewPlayer = slot == null
        if (slot == null) {
            check(players.size < 2) { "tic-tac-toe game already has two players" }
            val mark = if (players.size == 0) "X" else "O"
            slot = PlayerSlot(actor, mark)
            players += slot
        } else {
            slot.actor = actor
        }
        if (players.size == 2 && status == "WaitingForPlayers") {
            status = "InProgress"
            resetTurnDeadline()
        }
        actor.joinGame(gameId)
        val state = snapshot()
        val notified =
            if (isNewPlayer) {
                notifyPlayerJoined(actor, slot, state)
            } else {
                CompletableFuture.completedFuture(null)
            }
        return notified
            .thenCompose { broadcast(state, actor.actorId) }
            .thenApply { TicTacToeGameJoinRes(state) }
    }

    fun placeMark(actor: PlayActor, cell: Int): CompletionStage<PlaceMarkRes> {
        ensureCreated()
        val slot = players.firstOrNull { it.actor.actorId == actor.actorId }
            ?: throw IllegalStateException("player has not joined")
        check(status == "InProgress") { "game is not in progress" }
        check(slot.mark == nextTurn) { "unexpected turn" }
        require(cell in board.indices && board[cell] == '.') { "invalid cell" }

        board[cell] = slot.mark[0]
        lastMoveActorId = actor.actorId
        lastMoveCell = cell
        advance(slot)
        val state = snapshot()
        return broadcast(state, actor.actorId)
            .thenApply { PlaceMarkRes(state) }
    }

    fun hasPlayer(actorId: String): Boolean =
        players.any { it.actor.actorId == actorId }

    private fun snapshot(): GameState {
        ensureCreated()
        return GameState(
            gameId = gameId,
            board = String(board),
            status = status,
            winner = winnerValue,
            nextTurn = nextTurn,
            xActorId = players.firstOrNull { it.mark == "X" }?.actor?.actorId,
            oActorId = players.firstOrNull { it.mark == "O" }?.actor?.actorId,
            lastMoveActorId = lastMoveActorId,
            lastMoveCell = lastMoveCell,
        )
    }

    private fun advance(slot: PlayerSlot) {
        if (hasWon(slot.mark[0])) {
            status = "Won"
            winnerValue = slot.actor.actorId
            nextTurn = ""
            turnDeadline = null
        } else if ('.' !in board) {
            status = "Draw"
            winnerValue = null
            nextTurn = ""
            turnDeadline = null
        } else {
            nextTurn = if (slot.mark == "X") "O" else "X"
            resetTurnDeadline()
        }
    }

    fun tickAsync(): CompletionStage<Void> {
        ensureCreated()
        val deadline = turnDeadline
        if (status != "InProgress" || deadline == null || Instant.now().isBefore(deadline)) {
            return CompletableFuture.completedFuture(null)
        }

        val timedOut = players.firstOrNull { it.mark == nextTurn }
        val winner = players.firstOrNull { it.mark != nextTurn }

        status = "TurnTimedOut"
        winnerValue = winner?.actor?.actorId
        nextTurn = ""
        lastMoveActorId = timedOut?.actor?.actorId
        lastMoveCell = null
        turnDeadline = null

        return broadcast(snapshot(), null)
    }

    private fun resetTurnDeadline() {
        turnDeadline = Instant.now().plus(turnTimeout)
    }

    private fun ensureCreated() {
        check(created) { "tic-tac-toe game has not completed creation" }
    }

    private fun hasWon(mark: Char): Boolean {
        val lines = arrayOf(
            intArrayOf(0, 1, 2),
            intArrayOf(3, 4, 5),
            intArrayOf(6, 7, 8),
            intArrayOf(0, 3, 6),
            intArrayOf(1, 4, 7),
            intArrayOf(2, 5, 8),
            intArrayOf(0, 4, 8),
            intArrayOf(2, 4, 6),
        )
        return lines.any { (a, b, c) -> board[a] == mark && board[b] == mark && board[c] == mark }
    }

    private fun broadcast(state: GameState, excludedActorId: String?): CompletionStage<Void> =
        allOf(players
            .map { it.actor }
            .filter { excludedActorId == null || it.actorId != excludedActorId }
            .map { actor ->
                actor.context().boundSession()
                    .send(GameStateNotify(state))
                    .packetName("GameStateNotify")
                    .submitAsync()
            })

    private fun notifyPlayerJoined(
        joinedActor: PlayActor,
        joinedSlot: PlayerSlot,
        state: GameState,
    ): CompletionStage<Void> {
        val message = PlayerJoinedNotify(
            gameId = state.gameId,
            actorId = joinedActor.actorId,
            mark = joinedSlot.mark,
            state = state,
        )
        return allOf(players
            .map { it.actor }
            .filter { it.actorId != joinedActor.actorId }
            .map { actor ->
                actor.context().boundSession()
                    .send(message)
                    .packetName("PlayerJoinedNotify")
                    .submitAsync()
            })
    }

    private fun allOf(stages: List<CompletionStage<Void>>): CompletionStage<Void> =
        stages.fold(CompletableFuture.completedFuture<Void>(null)) { combined, stage ->
            combined.thenCompose { stage }
        }

    private data class PlayerSlot(var actor: PlayActor, val mark: String)
}
