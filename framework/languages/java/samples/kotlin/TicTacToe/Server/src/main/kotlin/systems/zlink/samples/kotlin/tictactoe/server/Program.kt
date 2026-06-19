package systems.zlink.samples.kotlin.tictactoe.server

import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServerApplication
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServerApplication

fun main(args: Array<String>) {
    runBlocking {
        val settings = SampleSettings.load(args)
        when (args.firstOrNull { !it.startsWith("--") } ?: error(usage())) {
            "api" -> ApiServerApplication.run(settings)
            "play" -> PlayServerApplication.run(settings)
            else -> error(usage())
        }
    }
}

private fun usage(): String =
    "Usage: gradle :Server:run --args='[play|api] [--config PATH] [--api-url URL] " +
        "[--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] " +
        "[--play-channel-endpoint tcp://HOST:PORT] " +
        "[--play-endpoint tcp://HOST:PORT] " +
        "[--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'"
