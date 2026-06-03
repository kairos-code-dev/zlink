package systems.zlink.samples.kotlin.tictactoe.client

data class TicTacToeClientOptions(
    val gameName: String,
    val hostAccessToken: String,
    val guestAccessToken: String,
    val apiEndpoint: String,
    val playEndpoint: String,
) {
    companion object {
        fun createDefault(): TicTacToeClientOptions =
            TicTacToeClientOptions(
                gameName = "Morning game",
                hostAccessToken = TicTacToeSampleDefaults.HostAccessToken,
                guestAccessToken = TicTacToeSampleDefaults.GuestAccessToken,
                apiEndpoint = TicTacToeSampleDefaults.ApiEndpoint,
                playEndpoint = TicTacToeSampleDefaults.PlayEndpoint,
            )
    }
}
