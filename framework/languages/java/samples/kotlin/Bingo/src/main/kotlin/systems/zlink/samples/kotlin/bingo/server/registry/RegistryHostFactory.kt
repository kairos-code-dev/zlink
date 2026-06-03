package systems.zlink.samples.kotlin.bingo.server.registry

import systems.zlink.framework.ZLinkRegistry
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology

object RegistryHostFactory {
    fun start(): ZLinkRegistry =
        ZLinkRegistry.start { options ->
            options.setPubEndpoint(SampleTopology.RegistryPubEndpoint)
            options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint)
        }
}
