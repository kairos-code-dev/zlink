package systems.zlink.samples.kotlin.bingo.server.session

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.samples.kotlin.bingo.server.session.sessions.BingoSession
import systems.zlink.samples.kotlin.bingo.server.session.sessions.handlers.AuthenticateSessionHandler
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology



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
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableClient()
            val route = options.addRouteMeshChannel(SampleNames.RoomRouteChannel)
            route.enableServer(SampleTopology.SessionRouteEndpoint)
            route.enableClient(SampleTopology.PlayRouteEndpoint)
            route.configureRouting().setRoutingId(RoutingId.from(SampleTopology.SessionRouterRid))
            options.useRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery)
                .setRouterChannelId(SampleNames.RoomRouteChannel)
            val node = options.addSpotMesh(SampleNames.RoomSpotDiscovery)
                .addNode(SampleNames.SessionSpotNode)
            node.enableRouter(SampleTopology.SessionRouterEndpoint)
                .setRouterRoutingId(RoutingId.from(SampleTopology.SessionRouterRid))
            node.enablePubSub(SampleTopology.SessionSpotEndpoint)
                .setPubSubRoutingId(RoutingId.from(SampleTopology.SessionPubRid))
            node.acceptSpotRoutesFromChannel(SampleNames.RoomRouteChannel)
            options.addStreamNode(SampleNames.StreamNode)
                .attachActorGateway(SampleNames.SessionSpotNode)
                .bind(SampleTopology.StreamEndpoint)
                .registerSession(BingoSession::class.java)
                .addSessionPacketHandler(AuthenticateSessionHandler::class.java)
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
