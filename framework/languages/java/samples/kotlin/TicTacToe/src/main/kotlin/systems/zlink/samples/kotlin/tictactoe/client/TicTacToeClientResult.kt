package systems.zlink.samples.kotlin.tictactoe.client

data class TicTacToeClientResult(
    val winner: String?,
    val pushes: List<String>,
)
