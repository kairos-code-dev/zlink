package systems.zlink.e2e.kotlin.spotservice.registry

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.registry"]
)
class RegistryApplication {
    @Bean
    fun registryOptions(): ZLinkEmbeddedRegistryOptions {
        val options = ZLinkEmbeddedRegistryOptions()
        options.setPubEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_PUB"))
        options.setRouterEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
        return options
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
