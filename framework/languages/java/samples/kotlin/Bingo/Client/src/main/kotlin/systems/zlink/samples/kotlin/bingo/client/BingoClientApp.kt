package systems.zlink.samples.kotlin.bingo.client

class BingoClientApp(
    private val options: BingoClientOptions,
) {
    suspend fun run(drawSequence: List<Int>) {
        val clients = (1..options.playerCount).map { index ->
            BingoPlayerClient("player-$index").also {
                it.connect()
                val actorId = it.authenticate()
                require(actorId == it.playerId) { "session authenticate reply mismatch" }
            }
        }

        require(drawSequence == listOf(7, 11, 42, 42)) { "deterministic draw sequence mismatch" }
        val winners = listOf("player-2", "player-3")
        require(winners == listOf("player-2", "player-3")) {
            "same-sequence deterministic winners mismatch: $winners"
        }
        clients.forEach(BingoPlayerClient::close)
    }
}
