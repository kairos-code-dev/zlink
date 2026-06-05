package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots

class TicTacToeGameSpot(
    val matchId: String,
    val ownerActorId: String,
) {
    private val board = ".........".toCharArray()
    private var xActorId: String? = null
    private var oActorId: String? = null
    private var turnActorId: String? = null
    private var winnerActorId: String? = null
    private var lastMoveActorId: String? = null
    private var lastMoveCell: Int? = null
    private var draw = false

    @Synchronized
    fun join(actorId: String): JoinResult {
        var isNewActor = false
        val mark = when (actorId) {
            xActorId -> "X"
            oActorId -> "O"
            else -> {
                when {
                    xActorId == null -> {
                        xActorId = actorId
                        turnActorId = actorId
                        isNewActor = true
                        "X"
                    }
                    oActorId == null -> {
                        oActorId = actorId
                        isNewActor = true
                        "O"
                    }
                    else -> throw IllegalStateException("Match is full: $matchId")
                }
            }
        }
        val state = snapshot()
        return JoinResult(matchId, actorId, mark, state, joinEvents(actorId, mark, state, isNewActor))
    }

    @Synchronized
    fun placeMark(actorId: String, cell: Int): MoveResult {
        if (actorId != turnActorId) {
            throw IllegalStateException("It is not $actorId's turn")
        }
        if (cell !in board.indices || board[cell] != '.') {
            throw IllegalArgumentException("Invalid move: $cell")
        }
        board[cell] = if (actorId == xActorId) 'X' else 'O'
        lastMoveActorId = actorId
        lastMoveCell = cell
        winnerActorId = winner()
        draw = winnerActorId == null && !board.contains('.')
        if (winnerActorId == null && !draw) {
            turnActorId = if (actorId == xActorId) oActorId else xActorId
        }
        val state = snapshot()
        val events = if (winnerActorId == null && !draw) turnChangedEvents(state) else gameEndedEvents(state)
        return MoveResult(state, events)
    }

    private fun winner(): String? {
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
        for (line in lines) {
            val mark = board[line[0]]
            if (mark != '.' && mark == board[line[1]] && mark == board[line[2]]) {
                return if (mark == 'X') xActorId else oActorId
            }
        }
        return null
    }

    private fun snapshot(): State =
        State(
            matchId = matchId,
            board = String(board),
            status = when {
                winnerActorId != null -> "Won"
                draw -> "Draw"
                else -> "Playing"
            },
            turnActorId = turnActorId,
            winnerActorId = winnerActorId,
            draw = draw,
            xActorId = xActorId,
            oActorId = oActorId,
            lastMoveActorId = lastMoveActorId,
            lastMoveCell = lastMoveCell,
        )

    private fun joinEvents(
        joinedActorId: String,
        joinedMark: String,
        state: State,
        isNewActor: Boolean,
    ): List<GameEvent> {
        val events = mutableListOf<GameEvent>()
        if (isNewActor) {
            actorIds()
                .filter { it != joinedActorId }
                .forEach { events += GameEvent.opponentJoined(it, joinedActorId, joinedMark, state) }
        }
        events += turnChangedEvents(state)
        return events
    }

    private fun turnChangedEvents(state: State): List<GameEvent> =
        if (state.status == "Playing") {
            actorIds().map { GameEvent.turnChanged(it, state) }
        } else {
            emptyList()
        }

    private fun gameEndedEvents(state: State): List<GameEvent> =
        actorIds().map { GameEvent.gameEnded(it, state) }

    private fun actorIds(): List<String> =
        listOfNotNull(xActorId, oActorId)

    data class JoinResult(
        val matchId: String,
        val actorId: String,
        val mark: String,
        val state: State,
        val events: List<GameEvent>,
    ) {
        fun encode(): String = "$matchId|$actorId|$mark|${state.encode()}"
    }

    data class MoveResult(
        val state: State,
        val events: List<GameEvent>,
    ) {
        fun encode(): String = state.encode()
    }

    data class GameEvent(
        val kind: String,
        val recipientActorId: String,
        val joinedActorId: String?,
        val joinedMark: String?,
        val state: State,
    ) {
        companion object {
            fun opponentJoined(
                recipientActorId: String,
                joinedActorId: String,
                joinedMark: String,
                state: State,
            ): GameEvent =
                GameEvent("OpponentJoined", recipientActorId, joinedActorId, joinedMark, state)

            fun turnChanged(recipientActorId: String, state: State): GameEvent =
                GameEvent("TurnChanged", recipientActorId, null, null, state)

            fun gameEnded(recipientActorId: String, state: State): GameEvent =
                GameEvent("GameEnded", recipientActorId, null, null, state)
        }
    }

    data class State(
        val matchId: String,
        val board: String,
        val status: String,
        val turnActorId: String?,
        val winnerActorId: String?,
        val draw: Boolean,
        val xActorId: String?,
        val oActorId: String?,
        val lastMoveActorId: String?,
        val lastMoveCell: Int?,
    ) {
        fun encode(): String =
            listOf(
                matchId,
                board,
                status,
                turnActorId.orEmpty(),
                winnerActorId.orEmpty(),
                draw.toString(),
                xActorId.orEmpty(),
                oActorId.orEmpty(),
                lastMoveActorId.orEmpty(),
                lastMoveCell?.toString().orEmpty(),
            ).joinToString(",")
    }
}
