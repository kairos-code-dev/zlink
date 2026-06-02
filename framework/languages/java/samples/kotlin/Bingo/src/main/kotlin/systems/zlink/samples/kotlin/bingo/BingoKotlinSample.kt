package systems.zlink.samples.kotlin.bingo

import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.bingo.client.BingoClientApp
import systems.zlink.samples.kotlin.bingo.client.BingoClientOptions
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot

fun main() = runBlocking {
    val app = BingoClientApp(BingoClientOptions(playerCount = 4))
    val room = BingoRoomSpot("room-1", listOf(7, 11, 42, 42))
    app.run(room)
    println("Bingo Kotlin sample self-check passed")
}
