package systems.zlink.samples.kotlin.bingo.server.play

import com.fasterxml.jackson.databind.MapperFeature
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.json.JsonMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.beans.factory.ObjectProvider
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.notifications.BingoNotificationPublisher
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.BingoRoomSpotCreatedHandler
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.handlers.BingoRoomDirectory
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [PlayServerApplication::class],
)
class PlayServerApplication {
    @Bean
    fun zlinkCoroutineRuntime(): ZLinkCoroutineRuntime =
        ZLinkCoroutineRuntime()

    @Bean
    fun playFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(PlayServerApplication::class.java)
            options.useDiscovery { discovery ->
                discovery.addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            }
            options.codecs().addProtobuf()
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
                        router.bindRouter(SampleTopology.PlaySpotRouterEndpoint)
                        router.setRoutingId(RoutingId.from(SampleTopology.PlayRid))
                    }
                    node.enablePubSub { pubSub -> pubSub.bindPubSub(SampleTopology.PlaySpotEndpoint) }
                    node.attachChannelClient(SampleNames.ApiChannel)
                    node.acceptSpotRoutesFromChannel(SampleNames.RoomRouteChannel)
                    node.addEntrySpot(BingoEntrySpot::class.java)
                    node.addSpotFactory(BingoRoomSpot::class.java)
                }
            }
        }

    @Bean
    fun bingoRoomDirectory(
        spots: ObjectProvider<ZLinkSpotManager>,
        json: ObjectMapper,
    ): BingoRoomDirectory =
        BingoRoomDirectory(spots.getObject(), json)

    @Bean
    fun bingoNotificationPublisher(): BingoNotificationPublisher = BingoNotificationPublisher()

    @Bean
    fun bingoRoomSpotCreatedHandler(json: ObjectMapper): BingoRoomSpotCreatedHandler =
        BingoRoomSpotCreatedHandler(json)

    @Bean
    fun bingoJsonMapper(): ObjectMapper =
        JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build()

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val builder = SpringApplicationBuilder(PlayServerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
