package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

class BingoNotificationPublisher {
    suspend fun publishWinner(client: BingoWinnerSink, winners: List<String>, roomId: String) {
        client.publishWinner(winners.joinToString(","), roomId)
    }
}
