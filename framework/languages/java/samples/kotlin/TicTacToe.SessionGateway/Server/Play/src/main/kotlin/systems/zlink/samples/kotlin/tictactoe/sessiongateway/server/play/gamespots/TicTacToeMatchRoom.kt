package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots

data class TicTacToeMatchRoom(
    val matchId: String,
    val lastActorId: String = "",
)
