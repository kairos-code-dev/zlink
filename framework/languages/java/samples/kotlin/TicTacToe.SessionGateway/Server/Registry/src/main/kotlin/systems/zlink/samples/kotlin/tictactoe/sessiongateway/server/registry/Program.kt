package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.registry

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

fun main(args: Array<String>) {
    RegistryServerApplication.start(args)
}

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [RegistryServerApplication::class],
)
class RegistryServerApplication {
    @Bean
    fun registryOptions(): ZLinkEmbeddedRegistryOptions =
        ZLinkEmbeddedRegistryOptions().also { options ->
            options.setPubEndpoint(SampleTopology.RegistryPubEndpoint)
            options.setRouterEndpoint(SampleTopology.RegistryRouterEndpoint)
        }

    companion object {
        fun start(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(RegistryServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
