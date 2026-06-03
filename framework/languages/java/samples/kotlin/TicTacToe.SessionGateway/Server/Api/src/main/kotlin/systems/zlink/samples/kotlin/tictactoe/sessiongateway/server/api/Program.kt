package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api

import java.util.concurrent.CountDownLatch
import systems.zlink.framework.ZLinkFramework
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry.RegistryServer

fun main() {
    ApiServerHostFactory.start().use {
        CountDownLatch(1).await()
    }
}

object ApiServerHostFactory {
    fun start(): ZLinkFramework =
        ZLinkFramework.start { options ->
            RegistryServer.configure(options)
            ApiServer.configure(options)
        }
}
