package systems.zlink.samples.kotlin.bingo.server.registry

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [RegistryHostFactory::class],
)
class RegistryHostFactory {
    @Bean
    fun registryOptions(): ZLinkEmbeddedRegistryOptions =
        ZLinkEmbeddedRegistryOptions().also { options ->
            options.setPubEndpoint(SampleTopology.RegistryPubEndpoint)
            options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint)
        }

    companion object {
        fun start(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(RegistryHostFactory::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
