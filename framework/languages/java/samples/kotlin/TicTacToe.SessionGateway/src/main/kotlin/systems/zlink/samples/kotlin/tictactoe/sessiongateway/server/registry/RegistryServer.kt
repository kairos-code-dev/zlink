package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry

import systems.zlink.framework.configuration.ZLinkFrameworkOptions

object RegistryServer {
    fun configure(options: ZLinkFrameworkOptions) {
        options.useDiscovery {
        }
    }
}
