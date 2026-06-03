package systems.zlink.samples.kotlin.tictactoe.server

import java.util.concurrent.CountDownLatch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import systems.zlink.framework.ZLinkFramework
import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiHttpServer
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main(args: Array<String>) = runBlocking {
    val settings = SampleSettings.fromArgs(args)
    when (args.firstOrNull { !it.startsWith("--") } ?: "all") {
        "all", "server" -> runServer(settings, PlayServer::configure, ApiServer::configure, startHttpApi = true)
        "api" -> runServer(settings, ApiServer::configure, startHttpApi = true)
        "play" -> runServer(settings, PlayServer::configure)
        else -> error(
            "Usage: gradle :Server:run --args='[all|server|api|play] [--api-url URL] [--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] [--play-channel-endpoint tcp://HOST:PORT] [--play-endpoint tcp://HOST:PORT] [--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]'",
        )
    }
}

private suspend fun runServer(
    settings: SampleSettings,
    vararg configureHosts: (ZLinkFrameworkOptions) -> Unit,
    startHttpApi: Boolean = false,
) {
    SampleSettings.setCurrent(settings)
    val hosts = mutableListOf<ZLinkFramework>()
    var httpApi: ApiHttpServer? = null
    try {
        configureHosts.forEach { configure -> hosts += ZLinkFramework.start(configure) }
        if (startHttpApi) {
            httpApi = ApiHttpServer.start(hosts.last().client(), settings)
        }
        withContext(Dispatchers.IO) {
            CountDownLatch(1).await()
        }
    } finally {
        httpApi?.close()
        hosts.asReversed().forEach { it.close() }
    }
}
