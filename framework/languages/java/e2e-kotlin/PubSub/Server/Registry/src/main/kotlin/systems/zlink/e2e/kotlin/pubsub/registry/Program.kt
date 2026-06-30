package systems.zlink.e2e.kotlin.pubsub.registry

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions

private lateinit var parsedOptions: RegistryOptions

fun main(args: Array<String>) {
    parsedOptions = RegistryOptions.parse(args)
    val builder = SpringApplicationBuilder(RegistryApplication::class.java)
        .web(WebApplicationType.NONE)
    builder.application().setKeepAlive(true)
    builder.run(*args)
}

@SpringBootApplication(proxyBeanMethods = false)
class RegistryApplication {
    @Bean
    fun pubSubRegistryOptions(): RegistryOptions = parsedOptions

    @Bean
    fun operationalEndpoints(options: RegistryOptions): OperationalEndpoints =
        OperationalEndpoints(options)

    @Bean
    fun registryOptions(options: RegistryOptions): ZLinkEmbeddedRegistryOptions {
        val registry = ZLinkEmbeddedRegistryOptions()
        registry.setPubEndpoint(options.pubEndpoint)
        registry.setRouterEndpoint(options.routerEndpoint)
        return registry
    }
}
