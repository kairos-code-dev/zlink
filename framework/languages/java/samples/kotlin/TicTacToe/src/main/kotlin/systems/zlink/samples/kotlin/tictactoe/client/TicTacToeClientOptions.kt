package systems.zlink.samples.kotlin.tictactoe.client

data class TicTacToeClientOptions(
    val gameName: String,
    val hostAccessToken: String,
    val guestAccessToken: String,
    val apiEndpoint: String,
    val playEndpoint: String,
)
