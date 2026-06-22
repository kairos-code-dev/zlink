package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots

import com.fasterxml.jackson.databind.ObjectMapper
import java.time.Duration
import java.time.Instant
import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkTimer
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.handlers.TicTacToeGameCreatedHandler
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.handlers.TicTacToeGameTimerHandler
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameState
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameStateNotify
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerInfo
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerJoinedNotify
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerWinMilestoneEvent
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinRes

class TicTacToeGame(
    private val context: ZLinkSpotContext,
    private val createdHandler: TicTacToeGameCreatedHandler,
    private val json: ObjectMapper,) : ZLinkSuspendingSpot<PlayActor>() {
    private val gameTickPeriod: Duration = Duration.ofSeconds(1)
    private val turnTimeout: Duration = Duration.ofSeconds(15)
    val roomId: String = context.spotRid().toString()
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
    private var cleanupStarted = false

    override fun context(): ZLinkSpotContext = context

    override suspend fun onCreateSuspending(request: Message): ZLinkSpotCreateResponse {
        createdHandler.handle(this, request)
        return ZLinkSpotCreateResponse.accept()
    }

    override suspend fun onActorJoinSuspending(
        actor: PlayActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        val joinRequest = json.readValue(request.toByteArray(), TicTacToeGameJoinReq::class.java)
        require(joinRequest.player.actorId == actor.actorId) {
            "join request actor id does not match bound actor"
        }
        val reply = join(actor, joinRequest.roomId, joinRequest.player)
        return ZLinkSpotActorJoinResponse.accept(Message.from(json.writeValueAsBytes(reply)))
    }

    override fun onJoinedActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) {
    }

    override fun onLeaveActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) {
        players.removeIf { it.actor.actorId == actor.actorId }
    }

    override fun onDisconnectActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) {
        actor.markDisconnected()
    }

    override suspend fun onInitializeSuspending() {
        gameTick = context.addTimer(
            "game-tick",
            gameTickPeriod,
            TicTacToeGameTimerHandler::class.java,
            ZLinkTimerOptions(),
        ).await()
    }

    override suspend fun onClosingSuspending() {
        gameTick?.cancelAsync()?.await()
    }

    fun markCreated(request: Message) {
        require(request.isEmpty()) { "tic-tac-toe game creation does not accept payload parts" }
        created = true
    }

    suspend fun join(actor: PlayActor, roomId: String, player: PlayerInfo): TicTacToeGameJoinRes {
        ensureCreated()
        check(roomId == this.roomId) { "join request room id does not match game room" }
        check(player.level >= SampleNames.RequiredLevel) { "player level does not satisfy room requirement" }
        actor.applyPlayer(player)
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
        actor.joinGame(roomId)
        val state = snapshot()
        if (isNewPlayer) {
            notifyPlayerJoined(actor, slot, state)
        }
        broadcast(state, actor.actorId)
        return TicTacToeGameJoinRes(state)
    }

    suspend fun placeMark(actor: PlayActor, cell: Int): PlaceMarkRes {
        ensureCreated()
        val slot = players.firstOrNull { it.actor.actorId == actor.actorId }
            ?: throw IllegalStateException("player has not joined")
        check(status == "InProgress") { "game is not in progress" }
        check(slot.mark == nextTurn) { "unexpected turn" }
        require(cell in board.indices && board[cell] == '.') { "invalid cell" }

        val before = snapshot()
        board[cell] = slot.mark[0]
        lastMoveActorId = actor.actorId
        lastMoveCell = cell
        advance(slot)
        val state = snapshot()
        broadcast(state, actor.actorId)
        publishWinMilestone(actor, before, state)
        return PlaceMarkRes(state)
    }

    fun hasPlayer(actorId: String): Boolean =
        players.any { it.actor.actorId == actorId }

    private fun snapshot(): GameState {
        ensureCreated()
        return GameState(
            roomId = roomId,
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

    suspend fun tick() {
        ensureCreated()
        val deadline = turnDeadline
        if (status != "InProgress" || deadline == null || Instant.now().isBefore(deadline)) {
            leaveFinishedActors(snapshot())
            return
        }

        val timedOut = players.firstOrNull { it.mark == nextTurn }
        val winner = players.firstOrNull { it.mark != nextTurn }

        status = "TurnTimedOut"
        winnerValue = winner?.actor?.actorId
        nextTurn = ""
        lastMoveActorId = timedOut?.actor?.actorId
        lastMoveCell = null
        turnDeadline = null

        val state = snapshot()
        broadcast(state, null)
        leaveFinishedActors(state)
    }

    private suspend fun leaveFinishedActors(state: GameState) {
        if (cleanupStarted || !isTerminal(state)) {
            return
        }
        cleanupStarted = true
        for (slot in players.toList()) {
            slot.actor.markForDestroyAfterRoomLeave()
            context.leaveActor(slot.actor).await()
        }
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

    private suspend fun broadcast(state: GameState, excludedActorId: String?) {
        players
            .asSequence()
            .map { it.actor }
            .filter { excludedActorId == null || it.actorId != excludedActorId }
            .forEach { actor ->
                actor.context().boundSession()
                    .send(GameStateNotify(state))
                    .submit()
                    .await()
            }
    }

    private suspend fun notifyPlayerJoined(
        joinedActor: PlayActor,
        joinedSlot: PlayerSlot,
        state: GameState,
    ) {
        val player = joinedActor.requirePlayer()
        val message = PlayerJoinedNotify(
            roomId = state.roomId,
            actorId = joinedActor.actorId,
            displayName = player.displayName,
            level = player.level,
            mark = joinedSlot.mark,
            state = state,
        )
        players
            .asSequence()
            .map { it.actor }
            .filter { it.actorId != joinedActor.actorId }
            .forEach { actor ->
                actor.context().boundSession()
                    .send(message)
                    .submit()
                    .await()
            }
    }

    private data class PlayerSlot(var actor: PlayActor, val mark: String)

    private fun isTerminal(state: GameState): Boolean =
        state.status == "TurnTimedOut"

    fun leaveGame(actor: PlayActor, roomId: String) {
        check(this.roomId == roomId) { "leave request room id does not match game room" }
        actor.markForDestroyAfterRoomLeave()
        ZLinkAwait.await(context.leaveActor(actor))
        println("actor: LeaveGameReq completed. actor=${actor.actorId}")
    }

    private suspend fun publishWinMilestone(
        actor: PlayActor,
        before: GameState,
        after: GameState,
    ) {
        if (after.status != "Won" || before.status == "Won" || after.winner != actor.actorId) {
            return
        }
        val player = actor.requirePlayer()
        val wins = actor.incrementWins()
        if (player.wins < 99 || wins != 100) {
            return
        }
        context.outbound()
            .publish(
                SampleNames.PlayerMilestoneTopic,
                PlayerWinMilestoneEvent(
                    roomId = after.roomId,
                    actorId = actor.actorId,
                    displayName = player.displayName,
                    wins = wins,
                ),
            )
            .await()
    }
}
