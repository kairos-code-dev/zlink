package systems.zlink.samples.kotlin.tictactoe.client

import kotlinx.coroutines.runBlocking
import systems.zlink.framework.ZLinkFramework

fun main() = runBlocking {
    ZLinkFramework.start { options ->
        options.addClientServerChannel("tictactoe-api") { channel ->
            channel.enableClient { client ->
                client.useManualConnections { endpoints ->
                    endpoints.connect("tcp://127.0.0.1:47301")
                }
            }
        }
    }.use { framework ->
        val result = TicTacToeClient(framework.client()).run(
            TicTacToeClientOptions(
                gameName = "Morning game",
                hostAccessToken = "alice-token",
                guestAccessToken = "bob-token",
                apiEndpoint = "tcp://127.0.0.1:47301",
                playEndpoint = "tcp://127.0.0.1:47302",
            ),
        )
        result.writeTo(System.out)
    }
}
