package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

class BingoRoomState(
    private val roomId: String,
    private val drawSequence: List<Int>,
) {
    private val publisher = BingoNotificationPublisher()
    private val clients = mutableListOf<BingoWinnerSink>()
    private val cards = mutableMapOf<String, BingoCard>()
    var host = ""
        private set

    fun join(client: BingoWinnerSink) {
        if (clients.isEmpty()) {
            host = client.playerId
        }
        clients += client
        cards[client.playerId] = cardFor(client.playerId)
    }

    suspend fun start(playerId: String): Boolean {
        if (host != playerId) {
            return false
        }
        val winners = winners()
        clients.forEach { client -> publisher.publishWinner(client, winners, roomId) }
        return true
    }

    fun winners(): List<String> =
        cards.filterValues { card -> drawSequence.containsAll(card.numbers) }
            .keys
            .sorted()

    private fun cardFor(playerId: String): BingoCard = BingoRoomSpot.cardFor(playerId)
}
