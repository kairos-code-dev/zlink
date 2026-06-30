package systems.zlink.e2e.kotlin.discoveryregistryha.embedded

import systems.zlink.e2e.kotlin.discoveryregistryha.provider.ProviderApplication
import systems.zlink.e2e.kotlin.discoveryregistryha.registry.RegistryApplication

object EmbeddedApplication {
    fun run(args: Array<String>) {
        RegistryApplication.run(*args)
        ProviderApplication.run(*args)
    }
}
