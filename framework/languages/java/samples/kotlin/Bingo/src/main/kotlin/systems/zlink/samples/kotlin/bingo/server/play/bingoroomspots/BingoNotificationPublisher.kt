package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

import systems.zlink.samples.kotlin.bingo.client.BingoPlayerClient

class BingoNotificationPublisher {
    suspend fun publishWinner(client: BingoPlayerClient, winners: List<String>, roomId: String) {
        client.push("BingoWinner", "Winner:${winners.joinToString(",")}", roomId)
    }
}
