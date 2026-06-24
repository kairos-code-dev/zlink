package systems.zlink.samples.kotlin.bingo.server.play

import com.fasterxml.jackson.databind.MapperFeature
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.json.JsonMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.framework.configuration.RouteMeshChannelBuilder
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.matchmaking.RedisBingoMatchQueue
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.handlers.BingoRoomSpotCreatedHandler
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoMatchQueue
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocator
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology



@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [PlayServerApplication::class],
)
class PlayServerApplication {
    @Bean
    fun playFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(PlayServerApplication::class.java)
            options.useDiscovery().addRegistryEndpoint(SampleTopology.RegistryRouterEndpoint)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("BINGO_LOG_DIR") ?: "logs") + "/flow-play.log")
                traceNodeId("play")
            }
            options.codecs().addJson()
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            val route: RouteMeshChannelBuilder = options.addRouteMesh(SampleNames.PlayChannel)
            route.enableServer(SampleTopology.selectedPlayRouteEndpoint())
            route.enableClient()
            route.addHandlerGroup("play-route")
            route.setRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()))
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
            val node: ZLinkSpotNodeBuilder = options.addSpotMesh(SampleNames.RoomSpotDiscovery)
                .useRegistrySpotResolver()
                )
            node.enableRouter(SampleTopology.selectedPlaySpotRouterEndpoint())
                .setRouterRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()))
            node.enablePubSub(SampleTopology.selectedPlaySpotEndpoint())
                .setPubSubRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()))
            node.addEntrySpot(BingoEntrySpot::class.java)
            node.addSpotFactory(BingoRoomSpot::class.java)
        }

    @Bean
    fun bingoRoomAllocator(matchQueue: BingoMatchQueue): BingoRoomAllocator =
        BingoRoomAllocator(matchQueue, SampleTimings.DrawPeriod.toMillis())

    @Bean
    fun redisBingoMatchQueue(): BingoMatchQueue = RedisBingoMatchQueue()

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
