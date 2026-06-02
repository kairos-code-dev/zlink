package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots

import java.time.Duration
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotOutbound
import systems.zlink.framework.spots.ZLinkTimer
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameState
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes

class TicTacToeGame(
    val gameId: String = "room-1",
    val gameName: String = "Sample game",
) : ZLinkSpot {
    private val board = ".........".toCharArray()
    private val players = mutableListOf<PlayerSlot>()
    val pushes = mutableListOf<String>()
    private var status = "WaitingForPlayers"
    private var nextTurn = "X"
    private var winnerValue: String? = null
    private var lastMoveActorId: String? = null
    private var lastMoveCell: Int? = null

    override fun context(): ZLinkSpotContext = SampleSpotContext(gameId)
    override fun onCreateAsync(createParts: MutableList<Message>): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)

    fun join(actorId: String): JoinGameRes {
        if (players.none { it.actorId == actorId }) {
            val mark = if (players.isEmpty()) "X" else "O"
            players += PlayerSlot(actorId, mark)
            pushes += "PlayerJoined:$actorId"
        }
        if (players.size == 2 && status == "WaitingForPlayers") {
            status = "InProgress"
        }
        return JoinGameRes(snapshot())
    }

    fun placeMark(actorId: String, cell: Int): PlaceMarkRes {
        val slot = players.firstOrNull { it.actorId == actorId }
            ?: throw IllegalStateException("player has not joined")
        check(status == "InProgress") { "game is not in progress" }
        check(slot.mark == nextTurn) { "unexpected turn" }
        require(cell in board.indices && board[cell] == '.') { "invalid cell" }

        board[cell] = slot.mark[0]
        lastMoveActorId = actorId
        lastMoveCell = cell
        advance(slot)
        pushes += "GameStateChanged:$status"
        if (status == "Won") {
            pushes += "GameWon:$winnerValue"
        }
        return PlaceMarkRes(snapshot())
    }

    private fun snapshot(): GameState = GameState(
        gameId = gameId,
        board = String(board),
        status = status,
        winner = winnerValue,
        nextTurn = nextTurn,
        xActorId = players.firstOrNull { it.mark == "X" }?.actorId,
        oActorId = players.firstOrNull { it.mark == "O" }?.actorId,
        lastMoveActorId = lastMoveActorId,
        lastMoveCell = lastMoveCell,
    )

    private fun advance(slot: PlayerSlot) {
        if (hasWon(slot.mark[0])) {
            status = "Won"
            winnerValue = slot.actorId
            nextTurn = ""
        } else if ('.' !in board) {
            status = "Draw"
            nextTurn = ""
        } else {
            nextTurn = if (slot.mark == "X") "O" else "X"
        }
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

    private data class PlayerSlot(val actorId: String, val mark: String)

    private class SampleSpotContext(private val gameId: String) : ZLinkSpotContext {
        override fun spotRid(): RoutingId = RoutingId.from(gameId)
        override fun nodeRid(): RoutingId = RoutingId.from("play-node")
        override fun outbound(): ZLinkSpotOutbound = throw UnsupportedOperationException("not needed by sample")
        override fun leaveActorAsync(actor: ZLinkActor): CompletionStage<Void> =
            CompletableFuture.completedFuture(null)

        override fun addTimer(
            name: String,
            period: Duration,
            handlerType: Class<*>,
            options: ZLinkTimerOptions,
        ): CompletionStage<ZLinkTimer> = throw UnsupportedOperationException("not needed by sample")
    }
}
