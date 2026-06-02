package systems.zlink.samples.kotlin.tictactoe

import kotlinx.coroutines.runBlocking
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClient
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClientOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main() = runBlocking {
    ZLinkFramework.start { options ->
        ApiServer.configure(options)
        PlayServer.configure(options)
    }.use { framework ->
        val result = TicTacToeClient(framework.client()).run(
            TicTacToeClientOptions(
                gameName = "Morning game",
                hostAccessToken = "alice-token",
                guestAccessToken = "bob-token",
            ),
        )
        require(result.winner == "alice") { "direct TicTacToe winner mismatch" }
        require("GameWon:alice" in result.pushes) { "room Spot did not publish winner push" }
    }

    println("TicTacToe Kotlin sample self-check passed")
}
