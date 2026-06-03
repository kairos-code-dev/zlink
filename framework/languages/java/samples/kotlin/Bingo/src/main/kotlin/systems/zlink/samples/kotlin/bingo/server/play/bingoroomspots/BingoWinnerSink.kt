package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

interface BingoWinnerSink {
    val playerId: String

    suspend fun publishWinner(payload: String, roomId: String)
}
