package systems.zlink.samples.kotlin.tictactoe.server

import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClient
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClientOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServerHostFactory
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServerHostFactory

fun main(args: Array<String>) {
    runBlocking {
        val settings = SampleSettings.fromArgs(args)
        when (args.firstOrNull { !it.startsWith("--") } ?: "all") {
            "all" -> runAll(settings)
            "api" -> ApiServerHostFactory.start(settings)
            "play" -> PlayServerHostFactory.start(settings)
            "client" -> runClient(settings)
            else -> error(usage())
        }
    }
}

private fun usage(): String =
    "Usage: gradle :Server:run --args='[all|play|api|client] [--api-url URL] " +
        "[--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] " +
        "[--play-channel-endpoint tcp://HOST:PORT] " +
        "[--play-router-endpoint tcp://HOST:PORT] " +
        "[--play-endpoint tcp://HOST:PORT] " +
        "[--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'"

private suspend fun runAll(settings: SampleSettings) {
    val effectiveSettings = settings.withEphemeralDefaults()
    TicTacToeServerHostFactory.start(effectiveSettings).use {
        runClient(effectiveSettings)
    }
}

private suspend fun runClient(settings: SampleSettings) {
    val defaults = TicTacToeClientOptions.createDefault()
    val result = TicTacToeClient().run(defaults.copy(apiUrl = settings.apiPublicUrl))
    result.writeTo(System.out)
}
