package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry

import java.util.concurrent.CountDownLatch
import systems.zlink.framework.ZLinkRegistry
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

fun main() {
    RegistryHostFactory.start().use {
        CountDownLatch(1).await()
    }
}

object RegistryHostFactory {
    fun start(): ZLinkRegistry =
        ZLinkRegistry.start { options ->
            options.setPubEndpoint(SampleTopology.RegistryPubEndpoint)
            options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint)
        }
}
