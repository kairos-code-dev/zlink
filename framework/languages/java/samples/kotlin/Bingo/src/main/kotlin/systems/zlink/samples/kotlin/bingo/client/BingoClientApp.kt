package systems.zlink.samples.kotlin.bingo.client

import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomState

class BingoClientApp(
    private val options: BingoClientOptions,
) {
    suspend fun run(room: BingoRoomState) {
        val clients = (1..options.playerCount).map { index ->
            BingoPlayerClient("player-$index").also { it.connect() }
        }

        clients.forEach(room::join)
        require(room.host == "player-1") { "first joiner must be host" }
        require(!room.start("player-2")) { "non-host start must be rejected" }
        require(room.start("player-1")) { "host start must succeed" }

        val winners = room.winners()
        require(winners == listOf("player-2", "player-3")) {
            "same-sequence deterministic winners mismatch: $winners"
        }
        clients.forEach { client ->
            awaitNotification(client, "Winner:player-2,player-3")
        }
    }

    private suspend fun awaitNotification(client: BingoPlayerClient, expected: String) {
        val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(5)
        while (System.nanoTime() < deadline) {
            client.dispatch()
            if (expected in client.inbox.events()) {
                return
            }
            Thread.onSpinWait()
        }
        error("bound push did not arrive at ${client.playerId}")
    }
}
