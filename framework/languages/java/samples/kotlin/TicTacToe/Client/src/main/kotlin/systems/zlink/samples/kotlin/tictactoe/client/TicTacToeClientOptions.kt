package systems.zlink.samples.kotlin.tictactoe.client

data class TicTacToeClientOptions(
    val apiUrl: String,
    val gameName: String,
    val xActorId: String,
    val oActorId: String,
) {
    companion object {
        fun createDefault(): TicTacToeClientOptions =
            TicTacToeClientOptions(
                apiUrl = TicTacToeSampleDefaults.ApiUrl,
                gameName = TicTacToeSampleDefaults.GameName,
                xActorId = TicTacToeSampleDefaults.XActorId,
                oActorId = TicTacToeSampleDefaults.OActorId,
            )
    }
}
