package systems.zlink.samples.kotlin.tictactoe

import kotlinx.coroutines.runBlocking
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClient
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClientOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiHttpServer
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main(args: Array<String>) = runBlocking {
    val settings = SampleSettings.fromArgs(args).withEphemeralDefaults()
    SampleSettings.setCurrent(settings)
    ZLinkFramework.start { options -> PlayServer.configure(options, settings) }.use {
        ZLinkFramework.start { options -> ApiServer.configure(options, settings) }.use { api ->
            ApiHttpServer.start(api.client(), settings).use {
                val result = TicTacToeClient().run(
                    TicTacToeClientOptions(
                        apiUrl = settings.apiPublicUrl,
                        gameName = "tictactoe-game",
                        xActorId = "player-x",
                        oActorId = "player-o",
                    ),
                )
                require(result.winner == "player-x") { "direct TicTacToe winner mismatch" }
                require("GameWon:player-x" in result.pushes) { "room Spot did not publish winner push" }
            }
        }
    }

    println("TicTacToe Kotlin sample self-check passed")
}
