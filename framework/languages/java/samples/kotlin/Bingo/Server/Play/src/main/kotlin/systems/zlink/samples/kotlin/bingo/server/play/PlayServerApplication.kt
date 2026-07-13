package systems.zlink.samples.kotlin.bingo.server.play

import com.fasterxml.jackson.databind.MapperFeature
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.json.JsonMapper
import kotlinx.coroutines.Dispatchers
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActorFactory
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActorTransferAdapter
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.matchmaking.RedisBingoMatchQueue
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers.BingoRoomSettingsInitializer
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoMatchQueue
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocator
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleLocationStore
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.server.configuration.BingoMetricsReporter
import systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy
import io.micrometer.core.instrument.MeterRegistry



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
            options.useCoroutineHandlers(Dispatchers.Default)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("BINGO_LOG_DIR") ?: "logs") + "/flow-play.log")
                traceLabel("play")
            }
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.configureLocations()
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            options.addClientServerChannel(SampleNames.PlayChannel)
                .enableServer(SampleTopology.selectedPlayChannelEndpoint())
                .enableClient()
                .addHandlerGroup("play-route")
                .setRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()))
            val node: ZLinkSpotMeshBuilder = options.addSpotMesh(SampleNames.RoomSpotDiscovery)
            node.useDrainPolicy(ZLinkSpotDrainPolicy.DRAIN_NATURAL)
            node.enableRouter(SampleTopology.selectedPlaySpotRouterEndpoint())
                .setRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()))
            node.configureEntrySpot()
                .setRoutingId(RoutingId.from(SampleTopology.selectedPlayNodeRid()))
            node.enablePubSub(SampleTopology.selectedPlaySpotEndpoint())
            node.connectPeerPub(SampleTopology.peerPlaySpotEndpoint())
            node.addEntrySpot(BingoEntrySpot::class.java)
            node.addSpotFactory(BingoRoomSpot::class.java)
            node.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory::class.java)
            node.addActorTransferAdapter(
                SampleNames.PlayerActorType,
                PlayerActorTransferAdapter::class.java,
            )
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    @Bean
    fun bingoRoomAllocator(matchQueue: BingoMatchQueue): BingoRoomAllocator =
        BingoRoomAllocator(matchQueue, SampleTimings.DrawPeriod.toMillis())

    @Bean
    fun redisBingoMatchQueue(): BingoMatchQueue = RedisBingoMatchQueue()

    @Bean
    fun bingoRoomSettingsInitializer(): BingoRoomSettingsInitializer =
        BingoRoomSettingsInitializer()

    @Bean
    fun bingoJsonMapper(): ObjectMapper =
        JsonMapper.builder()
            .configure(MapperFeature.ACCEPT_CASE_INSENSITIVE_PROPERTIES, true)
            .configure(MapperFeature.USE_STD_BEAN_NAMING, true)
            .findAndAddModules()
            .build()

    @Bean(destroyMethod = "close")
    fun bingoMetricsReporter(registry: MeterRegistry): BingoMetricsReporter =
        BingoMetricsReporter(registry, "play")

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
