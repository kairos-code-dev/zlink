package systems.zlink.samples.kotlin.tictactoe.server

import kotlinx.coroutines.runBlocking
import org.springframework.context.ConfigurableApplicationContext
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClient
import systems.zlink.samples.kotlin.tictactoe.client.TicTacToeClientOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main(args: Array<String>) = runBlocking {
    val settings = SampleSettings.fromArgs(args)
    when (args.firstOrNull { !it.startsWith("--") } ?: "all") {
        "all", "server" -> runServer(settings, startPlay = true, startApi = true)
        "api" -> runServer(settings, startPlay = false, startApi = true)
        "play" -> runServer(settings, startPlay = true, startApi = false)
        "client" -> runClient(settings)
        else -> error(
            "Usage: gradle :Server:run --args='[all|server|api|play|client] [--api-url URL] [--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] [--play-channel-endpoint tcp://HOST:PORT] [--play-router-endpoint tcp://HOST:PORT] [--play-endpoint tcp://HOST:PORT] [--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'",
        )
    }
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

private fun runServer(
    settings: SampleSettings,
    startPlay: Boolean,
    startApi: Boolean,
) {
    startServer(settings, startPlay, startApi)
}

private suspend fun runClient(settings: SampleSettings) {
    val result = TicTacToeClient().run(
        TicTacToeClientOptions(
            apiUrl = settings.apiPublicUrl,
            gameName = "tictactoe-game",
            xActorId = "player-x",
            oActorId = "player-o",
        ),
    )
    result.writeTo(System.out)
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
