package systems.zlink.samples.kotlin.bingo.server.session

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
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
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("BINGO_LOG_DIR") ?: "logs") + "/flow-session.log")
                traceNodeId("session")
            }
            options.codecs().addJson()
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            val route = options.addRouteMesh(SampleNames.PlayChannel)
            route.enableServer(SampleTopology.selectedSessionRouteEndpoint())
            route.enableClient()
            route.setRoutingId(RoutingId.from(SampleTopology.selectedSessionRouteRid()))
            val node = options.addSpotMesh(SampleNames.RoomSpotDiscovery)

            node.enableRouter(SampleTopology.selectedSessionRouterEndpoint())
                .setRouterRoutingId(RoutingId.from(SampleTopology.selectedSessionRouterRid()))
            options.addStreamNode(SampleNames.StreamNode)
                .attachActorGateway(SampleNames.SessionSpotNode)
                .bind(SampleTopology.selectedStreamEndpoint())
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
