package systems.zlink.samples.kotlin.shoppingmallcheckout.server.registry

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.SampleTopology

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [RegistryApplication::class],
)
class RegistryApplication {
    @Bean
    fun registryOptions(): ZLinkEmbeddedRegistryOptions {
        val options = ZLinkEmbeddedRegistryOptions()
        options.setPubEndpoint(SampleTopology.RegistryPubEndpoint)
        options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint)
        return options
    }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(RegistryApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
