package systems.zlink.e2e.kotlin.resiliencelifecycle

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.resiliencelifecycle.registry"],
)
open class RegistryApplication {
    @Bean
    open fun serverOptions(): ServerOptions =
        ServerOptions.fromEnv()

    @Bean
    open fun registryOptions(options: ServerOptions): ZLinkEmbeddedRegistryOptions =
        ZLinkEmbeddedRegistryOptions().also {
            it.setPubEndpoint(options.registryPubEndpoint)
            it.setRouterEndpoint(options.registryRouterEndpoint)
        }

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(RegistryApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
