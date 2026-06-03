package systems.zlink.samples.kotlin.tictactoe

import kotlinx.coroutines.runBlocking
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClient
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClientOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main() = runBlocking {
    ZLinkFramework.start(PlayServer::configure).use {
        ZLinkFramework.start(ApiServer::configure).use { framework ->
            ZLinkFramework.start { options ->
                options.addClientServerChannel("tictactoe-api") { channel ->
                    channel.enableClient { client ->
                        client.useManualConnections { endpoints ->
                            endpoints.connect(SampleTopology.ApiEndpoint)
                        }
                    }
                }
            }.use { clientFramework ->
                val result = TicTacToeClient(clientFramework.client()).run(
                    TicTacToeClientOptions(
                        gameName = "Morning game",
                        hostAccessToken = "alice-token",
                        guestAccessToken = "bob-token",
                        apiEndpoint = SampleTopology.ApiEndpoint,
                        playEndpoint = SampleTopology.PlayStreamEndpoint,
                    ),
                )
                require(result.winner == "alice") { "direct TicTacToe winner mismatch" }
                require("GameWon:alice" in result.pushes) { "room Spot did not publish winner push" }
            }
        }
    }

    println("TicTacToe Kotlin sample self-check passed")
}
