package systems.zlink.samples.kotlin.tictactoe.server

import org.springframework.context.ConfigurableApplicationContext
import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClient
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClientOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main(args: Array<String>) {
    runBlocking {
        val settings = SampleSettings.fromArgs(args)
        when (args.firstOrNull { !it.startsWith("--") } ?: "all") {
            "all" -> runAll(settings)
            "api" -> ApiServer.start(settings)
            "play" -> PlayServer.start(settings)
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
    startServer(effectiveSettings, startPlay = true, startApi = true).use {
        runClient(effectiveSettings)
    }
}

private suspend fun runClient(settings: SampleSettings) {
    val defaults = TicTacToeClientOptions.createDefault()
    val result = TicTacToeClient().run(defaults.copy(apiUrl = settings.apiPublicUrl))
    result.writeTo(System.out)
}

fun startServer(
    settings: SampleSettings,
    startPlay: Boolean,
    startApi: Boolean,
): ServerHost {
    SampleSettings.setCurrent(settings)
    var play: ConfigurableApplicationContext? = null
    var api: ConfigurableApplicationContext? = null
    try {
        if (startPlay) {
            play = PlayServer.start(settings)
        }
        if (startApi) {
            api = ApiServer.start(settings)
        }
        return ServerHost(play, api)
    } catch (error: RuntimeException) {
        api?.close()
        play?.close()
        throw error
    }
}

class ServerHost(
    private val play: ConfigurableApplicationContext?,
    private val api: ConfigurableApplicationContext?,
) : AutoCloseable {
    override fun close() {
        api?.close()
        play?.close()
    }
}
