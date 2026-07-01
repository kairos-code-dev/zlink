package systems.zlink.samples.kotlin.deliverydispatch.server.couriersession

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.server.couriersession.sessions.CourierSession

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [CourierSessionApplication::class],
)
class CourierSessionApplication {
    @Bean
    fun courierSessionFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(CourierSessionApplication::class.java)
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(
                    System.getenv().getOrDefault("DELIVERYDISPATCH_LOG_DIR", "logs") +
                        "/flow-courier-session.log",
                )
                .traceLabel("courier-session")
            options.addClientServerChannel(SampleNames.CourierChannel)
                .enableClient()
            val node = options.addSpotMesh(SampleNames.CourierSpotMesh)
            node.enableRouter(SampleTopology.CourierSessionSpotRouterEndpoint)
                .setRoutingId(RoutingId.from(SampleTopology.CourierSessionSpotNodeRid))
            options.addStreamNode(SampleNames.CourierStreamNode)
                .bind(SampleTopology.CourierStreamEndpoint)
                .registerSession(CourierSession::class.java)
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(CourierSessionApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
