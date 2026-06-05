package systems.zlink.samples.kotlin.bingo.server.api

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [ApiServerHostFactory::class],
)
class ApiServerHostFactory {
    @Bean
    fun apiOptions(): ZLinkFrameworkOptionsCustomizer =
        ZLinkFrameworkOptionsCustomizer { options ->
            options.addHandlersFromPackageOf(ApiServerHostFactory::class.java)
            options.useDiscovery { discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            }
            options.codecs().addJson()
            options.addClientServerChannel(SampleNames.ApiChannel) { channel ->
                channel.enableServer { server -> server.bind(SampleTopology.ApiChannelEndpoint) }
                channel.addHandlerGroup("api")
            }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel -> channel.enableClient() }
        }

    companion object {
        fun start(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(ApiServerHostFactory::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
