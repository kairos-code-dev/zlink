package systems.zlink.samples.kotlin.bingo.client

import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot

class BingoClientApp(
    private val options: BingoClientOptions,
) {
    suspend fun run(room: BingoRoomSpot) {
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
            client.dispatch()
            require("Winner:player-2,player-3" in client.inbox.events()) {
                "bound push did not arrive at ${client.playerId}"
            }
        }
    }
}
