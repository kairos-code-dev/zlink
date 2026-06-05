package systems.zlink.samples.kotlin.bingo.server.play

import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spring.ZLinkFrameworkOptionsCustomizer
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoNotificationPublisher
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.entryspot.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.server.play.handlers.BingoRoomDirectory
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology

@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [PlayServerHostFactory::class],
)
class PlayServerHostFactory {
    @Bean
    fun playOptions(): ZLinkFrameworkOptionsCustomizer =
        ZLinkFrameworkOptionsCustomizer { options ->
            options.addHandlersFromPackageOf(PlayServerHostFactory::class.java)
            options.useDiscovery { discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint) }
            options.codecs().addJson()
            options.addClientServerChannel(SampleNames.PlayChannel) { channel ->
                channel.enableServer { server -> server.bind(SampleTopology.PlayChannelEndpoint) }
                channel.addHandlerGroup("play")
            }
            options.addClientServerChannel(SampleNames.ApiChannel) { channel -> channel.enableClient() }
            options.addRouteMeshChannel(SampleNames.RoomRouteChannel) { route ->
                route.bind(SampleTopology.PlayRouteEndpoint)
                route.configureRouting { routing -> routing.setRoutingId(RoutingId.from(SampleTopology.PlayRid)) }
                route.useManualConnections { endpoints -> endpoints.connect(SampleTopology.SessionRouteEndpoint) }
            }
            options.useRegistrySpotRemoteAddresses(SampleNames.RoomSpotDiscovery) { registry ->
                registry.setRouterChannelId(SampleNames.RoomRouteChannel)
            }
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
            options.addSpotMesh(SampleNames.RoomSpotDiscovery) { mesh ->
                mesh.addNode(SampleNames.RoomSpotNode) { node ->
                    node.enableRouter { router ->
                        router.setRouterBind(SampleTopology.PlaySpotRouterEndpoint)
                        router.setRoutingId(RoutingId.from(SampleTopology.PlayRid))
                    }
                    node.enablePubSub { pubSub -> pubSub.setPubBind(SampleTopology.PlaySpotEndpoint) }
                    node.attachChannelClient(SampleNames.ApiChannel)
                    node.acceptSpotRoutesFromChannel(SampleNames.RoomRouteChannel)
                    node.addEntrySpot(BingoEntrySpot::class.java)
                    node.addSpotFactory(BingoRoomSpot::class.java)
                }
            }
        }

    @Bean
    fun bingoRoomDirectory(spots: ZLinkSpotManager): BingoRoomDirectory =
        BingoRoomDirectory(spots)

    @Bean
    fun bingoNotificationPublisher(): BingoNotificationPublisher = BingoNotificationPublisher()

    companion object {
        fun start(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(PlayServerHostFactory::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
