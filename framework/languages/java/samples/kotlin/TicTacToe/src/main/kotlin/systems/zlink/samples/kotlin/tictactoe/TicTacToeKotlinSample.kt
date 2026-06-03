package systems.zlink.samples.kotlin.tictactoe

import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClient
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClientOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main(args: Array<String>) = runBlocking {
    val settings = SampleSettings.fromArgs(args).withEphemeralDefaults()
    SampleSettings.setCurrent(settings)
    PlayServer.start(settings).use {
        ApiServer.start(settings).use {
            val result = TicTacToeClient().run(
                TicTacToeClientOptions(
                    apiUrl = settings.apiPublicUrl,
                    gameName = "tictactoe-game",
                    xActorId = "player-x",
                    oActorId = "player-o",
                ),
            )
            require(result.finalState.winner == "player-x") { "direct TicTacToe winner mismatch" }
            require(result.stateNotifications.isNotEmpty()) { "room Spot did not send GameStateNotify push" }
            require(result.playerJoinedNotifications.isNotEmpty()) { "room Spot did not send PlayerJoinedNotify push" }
        }
    }

    println("TicTacToe Kotlin sample self-check passed")
}
