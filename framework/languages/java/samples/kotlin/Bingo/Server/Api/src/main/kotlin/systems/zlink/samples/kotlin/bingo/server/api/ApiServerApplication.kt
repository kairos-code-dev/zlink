package systems.zlink.samples.kotlin.bingo.server.api

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [ApiServerApplication::class],
)
class ApiServerApplication {
    @Bean
    fun apiFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(ApiServerApplication::class.java)
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.codecs().addJson()
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableServer(SampleTopology.selectedApiChannelEndpoint())
                .addHandlerGroup("api")
            val route = options.addRouteMeshChannel(SampleNames.PlayChannel)
            route.enableClient(SampleTopology.PlayARouteEndpoint)
            route.enableClient(SampleTopology.PlayBRouteEndpoint)
            route.setRoutingId(RoutingId.from(SampleTopology.selectedApiRouteRid()))
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(ApiServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
