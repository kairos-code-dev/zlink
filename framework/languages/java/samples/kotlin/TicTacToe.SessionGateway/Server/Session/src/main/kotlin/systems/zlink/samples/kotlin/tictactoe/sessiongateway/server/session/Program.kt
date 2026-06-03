package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session

import java.util.concurrent.CountDownLatch
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.PlayServer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry.RegistryServer

fun main() {
    SessionServerHostFactory.start().use {
        CountDownLatch(1).await()
    }
}

object SessionServerHostFactory {
    fun start(): ZLinkFramework =
        ZLinkFramework.start { options ->
            RegistryServer.configure(options)
            PlayServer.configureSessionRelayNode(options)
            SessionServer.configure(options)
        }
}
