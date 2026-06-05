package systems.zlink.samples.kotlin.bingo.server.session

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer
import systems.zlink.samples.kotlin.bingo.server.session.sessions.BingoSession
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SessionServerHostFactory::class],
)
class SessionServerHostFactory {
    @Bean
    fun sessionOptions(): ZLinkFrameworkOptionsCustomizer =
        ZLinkFrameworkOptionsCustomizer { options ->
            options.useDiscovery { discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            }
            options.codecs().addJson()
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
            }
        }

    companion object {
        fun start(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(SessionServerHostFactory::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
