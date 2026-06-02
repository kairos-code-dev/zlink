package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry

import systems.zlink.framework.configuration.ZLinkFrameworkOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

object RegistryServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.useDiscovery { registry ->
            registry.add(SampleTopology.RegistryRouterEndpoint)
        }
    }
}
