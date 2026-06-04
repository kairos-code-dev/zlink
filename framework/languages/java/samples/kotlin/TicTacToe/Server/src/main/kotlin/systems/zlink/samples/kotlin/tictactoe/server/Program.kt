package systems.zlink.samples.kotlin.tictactoe.server

import org.springframework.context.ConfigurableApplicationContext
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main(args: Array<String>) {
    val settings = SampleSettings.fromArgs(args)
    when (args.firstOrNull { !it.startsWith("--") } ?: error(usage())) {
        "api" -> ApiServer.start(settings)
        "play" -> PlayServer.start(settings)
        else -> error(usage())
    }
}

private fun usage(): String =
    "Usage: gradle :Server:run --args='[api|play] [--api-url URL] " +
        "[--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] " +
        "[--play-channel-endpoint tcp://HOST:PORT] " +
        "[--play-router-endpoint tcp://HOST:PORT] " +
        "[--play-endpoint tcp://HOST:PORT] " +
        "[--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'"

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
