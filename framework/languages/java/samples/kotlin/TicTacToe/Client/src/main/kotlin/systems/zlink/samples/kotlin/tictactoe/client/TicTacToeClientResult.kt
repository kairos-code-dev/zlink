package systems.zlink.samples.kotlin.tictactoe.client

import java.io.PrintStream
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameHttpRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameState
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.GameStateNotify
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerJoinedNotify

data class TicTacToeClientResult(
    val game: CreateGameHttpRes,
    val xAuthentication: AuthenticateRes,
    val oAuthentication: AuthenticateRes,
    val xJoin: JoinGameRes,
    val oJoin: JoinGameRes,
    val moves: List<PlaceMarkRes>,
    val stateNotifications: List<GameStateNotify>,
    val playerJoinedNotifications: List<PlayerJoinedNotify>,
) {
    val finalState: GameState
        get() = moves.lastOrNull()?.state ?: oJoin.state

    fun writeTo(output: PrintStream) {
        output.println("api: created game ${game.gameId} at ${game.playEndpoint}")
        output.println("client-x: AuthenticateRes|${xAuthentication.actorId}")
        output.println("client-o: AuthenticateRes|${oAuthentication.actorId}")
        output.println("client-x: JoinGameRes|${xJoin.state.gameId}|X")
        output.println("client-o: JoinGameRes|${oJoin.state.gameId}|O")
        moves.forEach { move ->
            output.println(
                "move: PlaceMarkRes|board=${move.state.board}|status=${move.state.status}|next=${format(move.state.nextTurn)}|winner=${format(move.state.winner)}",
            )
        }
        playerJoinedNotifications.forEach { notify ->
            output.println(
                "notify: PlayerJoinedNotify|player=${notify.actorId}|mark=${notify.mark}|game=${notify.gameId}",
            )
        }
        output.println("notifications: ${stateNotifications.size} GameStateNotify packets received")
        output.println("notifications: ${playerJoinedNotifications.size} PlayerJoinedNotify packets received")
        output.println("final: board=${finalState.board}|status=${finalState.status}|winner=${format(finalState.winner)}")
    }

    private fun format(value: String?): String =
        value?.takeIf { it.isNotEmpty() } ?: "-"
}
