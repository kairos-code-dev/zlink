package systems.zlink.samples.kotlin.supportchat.server.session

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.supportchat.server.session.sessions.SupportChatSession
import systems.zlink.samples.kotlin.supportchat.server.session.sessions.handlers.AuthenticateSupportChatSessionHandler



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SessionServerApplication::class],
)
class SessionServerApplication {
    @Bean
    fun sessionFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(SessionServerApplication::class.java)
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            options.addClientServerChannel(SampleNames.SupportChannel)
                .enableClient()
            val route = options.addRouteMeshChannel(SampleNames.SupportRouteChannel)
            route.enableServer(SampleTopology.SessionRouteEndpoint)
            route.enableClient(SampleTopology.SupportRouteEndpoint)
            route.setRoutingId(RoutingId.from(SampleTopology.SessionRouterRid))
            options.useRegistrySpotRemoteAddresses(SampleNames.SupportSpotDiscovery)
                .setRouterChannelId(SampleNames.SupportRouteChannel)
            val node = options.addSpotMesh(SampleNames.SupportSpotDiscovery)
                .addNode(SampleNames.SessionSpotNode)
            node.enableRouter(SampleTopology.SessionRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.SessionRouterRid))
            node.enablePubSub(SampleTopology.SessionSpotEndpoint)
                .setPubSubRoutingId(RoutingId.from(SampleTopology.SessionPubRid))
            node.acceptSpotRoutesFromChannel(SampleNames.SupportRouteChannel)
            options.addStreamNode(SampleNames.StreamNode)
                .attachActorGateway(SampleNames.SessionSpotNode)
                .bind(SampleTopology.StreamEndpoint)
                .registerSession(SupportChatSession::class.java)
                .addSessionPacketHandler(AuthenticateSupportChatSessionHandler::class.java)
        }

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(SessionServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
