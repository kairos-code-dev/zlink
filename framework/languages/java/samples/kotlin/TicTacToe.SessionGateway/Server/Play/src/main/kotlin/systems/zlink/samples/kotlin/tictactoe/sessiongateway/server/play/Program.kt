package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play

import java.util.concurrent.CountDownLatch
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry.RegistryServer

fun main() {
    PlayServerHostFactory.start().use {
        CountDownLatch(1).await()
    }
}

object PlayServerHostFactory {
    fun start(): ZLinkFramework =
        ZLinkFramework.start { options ->
            RegistryServer.configure(options)
            PlayServer.configure(options)
        }
}
