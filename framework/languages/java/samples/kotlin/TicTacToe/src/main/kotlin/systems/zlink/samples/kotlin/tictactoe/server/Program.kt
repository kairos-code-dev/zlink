package systems.zlink.samples.kotlin.tictactoe.server

import java.util.concurrent.CountDownLatch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import systems.zlink.framework.ZLinkFramework
import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.server.api.ApiServer
import systems.zlink.samples.kotlin.tictactoe.server.play.PlayServer

fun main(args: Array<String>) = runBlocking {
    when (args.firstOrNull { !it.startsWith("--") } ?: "server") {
        "all", "server" -> runServer {
            ApiServer.configure(it)
            PlayServer.configure(it)
        }
        "api" -> runServer(ApiServer::configure)
        "play" -> runServer(PlayServer::configure)
        else -> error("Usage: gradle :Server:run --args='[all|server|api|play]'")
    }
}

private suspend fun runServer(configure: (ZLinkFrameworkOptions) -> Unit) {
    ZLinkFramework.start(configure).use {
        withContext(Dispatchers.IO) {
            CountDownLatch(1).await()
        }
    }
}
