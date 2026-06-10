package systems.zlink.samples.kotlin.bingo.server.session

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.bingo.server.session.sessions.BingoSession
import systems.zlink.samples.kotlin.bingo.server.session.sessions.handlers.AuthenticateSessionHandler
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SessionServerApplication::class],
)
class SessionServerApplication {
    @Bean
    fun zlinkCoroutineRuntime(): ZLinkCoroutineRuntime =
        ZLinkCoroutineRuntime()

    @Bean
    fun sessionFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(SessionServerApplication::class.java)
            options.useDiscovery { discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            }
            options.codecs().addProtobuf()
            options.addClientServerChannel(SampleNames.ApiChannel) { channel -> channel.enableClient() }
            options.addClientServerChannel(SampleNames.PlayChannel) { channel -> channel.enableClient() }
            options.addRouteMeshChannel(SampleNames.RoomRouteChannel) { route ->
                route.bind(SampleTopology.SessionRouteEndpoint)
                route.configureRouting { routing ->
                    routing.setRoutingId(RoutingId.from(SampleTopology.SessionRouterRid))
                }
                route.useManualConnections { endpoints -> endpoints.connect(SampleTopology.PlayRouteEndpoint) }
            }
            options.useRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery) { registry ->
                registry.setRouterChannelId(SampleNames.RoomRouteChannel)
            }
            options.addSpotMesh(SampleNames.RoomSpotDiscovery) { mesh ->
                mesh.addNode(SampleNames.SessionSpotNode) { node ->
                    node.enableRouter { router ->
                        router.bindRouter(SampleTopology.SessionRouterEndpoint)
                        router.setRoutingId(RoutingId.from(SampleTopology.SessionRouterRid))
                    }
                    node.enablePubSub { pubSub ->
                        pubSub.bindPubSub(SampleTopology.SessionSpotEndpoint)
                        pubSub.setRoutingId(RoutingId.from(SampleTopology.SessionPubRid))
                    }
                    node.acceptSpotRoutesFromChannel(SampleNames.RoomRouteChannel)
                }
            }
            options.addStreamNode(SampleNames.StreamNode) { stream ->
                stream.attachActorGateway(SampleNames.SessionSpotNode)
                stream.bind(SampleTopology.StreamEndpoint)
                stream.registerSession(BingoSession::class.java)
                stream.addSessionPacketHandler(AuthenticateSessionHandler::class.java)
            }
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
