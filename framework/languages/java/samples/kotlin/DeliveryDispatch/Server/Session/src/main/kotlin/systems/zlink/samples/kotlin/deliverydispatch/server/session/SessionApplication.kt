package systems.zlink.samples.kotlin.deliverydispatch.server.session

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.CustomerSession
import systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.CustomerSessionDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.handlers.DeliveryStatusFanoutHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.handlers.SubscribeDeliveryHandler
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SessionApplication::class],
)
class SessionApplication {
    @Bean
    fun customerSessionDirectory(): CustomerSessionDirectory = CustomerSessionDirectory()

    @Bean
    fun sessionFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("DELIVERYDISPATCH_LOG_DIR") ?: "logs") + "/flow-session.log")
                traceNodeId("session")
            }
            options.codecs().addJson()
            options.addHandlersFromPackageOf(SessionApplication::class.java)
            options.addClientServerChannel(SampleNames.TrackingChannel)
                .enableClient()
            options.addFanoutChannel(SampleNames.StatusFanoutChannel)
                .enableSubscriber()
                .addPublishHandler(
                    DeliveryStatusFanoutHandler::class.java,
                    DeliveryStatusNotify::class.java,
                    "DeliveryStatusNotify",
                )
            val route = options.addRouteMesh(SampleNames.SpotRouteChannel)
            route.enableServer(SampleTopology.SessionSpotRouteEndpoint)
            route.enableClient()
            route.setRoutingId(RoutingId.from(SampleTopology.SessionSpotNodeRid))
            options.useRegistrySpotRemoteAddresses(SampleNames.DeliverySpotDiscovery)
                .setRouterChannelId(SampleNames.SpotRouteChannel)
            val node = options.addSpotMesh(SampleNames.DeliverySpotDiscovery)

            node.enableRouter(SampleTopology.SessionSpotRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.SessionSpotNodeRid))
            node.enablePubSub(SampleTopology.SessionSpotEndpoint)
                .setPubSubRoutingId(RoutingId.from(SampleTopology.SessionSpotPubRid))
            options.addStreamNode(SampleNames.CustomerStreamNode)
                .attachActorGateway(SampleNames.SessionSpotNode)
                .bind(SampleTopology.SessionStreamEndpoint)
                .registerSession(CustomerSession::class.java)
                .addSessionPacketHandler(SubscribeDeliveryHandler::class.java)
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(SessionApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
