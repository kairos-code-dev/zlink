package systems.zlink.e2e.kotlin.registrymessaging.registry

import java.io.File
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.registrymessaging.registry.Configuration.ServerOptions
import systems.zlink.e2e.kotlin.registrymessaging.registry.Endpoints.RegistryEndpoints
import systems.zlink.e2e.kotlin.registrymessaging.registry.Infrastructure.EvidenceStore
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.framework.spring.ZLinkRegistryQueryClientCustomizer

private lateinit var parsedOptions: ServerOptions

fun main(args: Array<String>) {
    parsedOptions = ServerOptions.parse(args)
    File(parsedOptions.logDir).mkdirs()
    val context = SpringApplicationBuilder(RegistryApplication::class.java)
        .web(WebApplicationType.NONE)
        .run(*args)
    RegistryEndpoints(parsedOptions, context).start()
}

@SpringBootApplication(proxyBeanMethods = false)
class RegistryApplication {
    @Bean
    fun serverOptions(): ServerOptions = parsedOptions

    @Bean
    fun evidenceStore(): EvidenceStore = EvidenceStore()

    @Bean
    fun registryOptions(options: ServerOptions): ZLinkEmbeddedRegistryOptions {
        val registry = ZLinkEmbeddedRegistryOptions()
        registry.setPubEndpoint(options.registryPubEndpoint)
        registry.setRouterEndpoint(options.registryRouterEndpoint)
        return registry
    }

    @Bean
    fun registryQueryClient(options: ServerOptions): ZLinkRegistryQueryClientCustomizer =
        ZLinkRegistryQueryClientCustomizer { query -> query.setEndpoint(options.registryRouterEndpoint) }
}
