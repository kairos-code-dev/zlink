package systems.zlink.samples.kotlin.bingo

import kotlinx.coroutines.runBlocking
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkFramework
import systems.zlink.framework.kotlin.create
import systems.zlink.samples.kotlin.bingo.client.BingoClientApp
import systems.zlink.samples.kotlin.bingo.client.BingoClientOptions
import systems.zlink.samples.kotlin.bingo.client.BingoNotificationLoopbackServer
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomState

fun main() = runBlocking {
    BingoNotificationLoopbackServer(29100).use {
        ZLinkFramework.start { options ->
            options.addSpotMesh("bingo") { mesh ->
                mesh.addNode("play") { node -> node.addSpotFactory(BingoRoomSpot::class.java) }
            }
        }.use { framework ->
            framework.spotManager().create(BingoRoomSpot::class.java, RoutingId.from("room-1"))
            val app = BingoClientApp(BingoClientOptions(playerCount = 4))
            val room = BingoRoomState("room-1", listOf(7, 11, 42, 42))
            app.run(room)
        }
    }
    println("Bingo Kotlin sample self-check passed")
}
