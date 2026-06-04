package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleTopology

fun main(args: Array<String>) {
    ApiServerApplication.start(args)
}

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [ApiServer::class],
)
class ApiServerApplication {
    @Bean
    fun apiOptions(): ZLinkFrameworkOptionsCustomizer =
        ZLinkFrameworkOptionsCustomizer { options ->
            options.useDiscovery { registry ->
                registry.add(SampleTopology.RegistryRouterEndpoint)
            }
            ApiServer.configure(options)
        }

    companion object {
        fun start(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(ApiServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
