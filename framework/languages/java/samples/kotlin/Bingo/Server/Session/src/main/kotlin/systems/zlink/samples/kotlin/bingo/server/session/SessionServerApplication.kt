package systems.zlink.samples.kotlin.bingo.server.session

import kotlinx.coroutines.Dispatchers
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.kotlin.configureDispatch
import systems.zlink.framework.kotlin.useCoroutineHandlers
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleLocationStore
import systems.zlink.samples.kotlin.bingo.server.session.sessions.BingoSession
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.server.configuration.BingoMetricsReporter
import io.micrometer.core.instrument.MeterRegistry



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
            options.useCoroutineHandlers(Dispatchers.Default)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile((System.getenv("BINGO_LOG_DIR") ?: "logs") + "/flow-session.log")
                traceLabel("session")
            }
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.configureLocations()
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            val node = options.addSpotMesh(SampleNames.RoomSpotDiscovery)

            node.enableRouter(SampleTopology.selectedSessionRouterEndpoint())
                .setRoutingId(RoutingId.from(SampleTopology.selectedSessionRouterRid()))
            node.connectRouter(
                RoutingId.from(SampleTopology.preferredPlayNodeRid()),
                SampleTopology.preferredPlaySpotRouterEndpoint(),
            )
            node.enablePubSub(SampleTopology.selectedSessionSpotEndpoint())
            options.addStreamNode(SampleNames.StreamNode)
                .bind(SampleTopology.selectedStreamEndpoint())
                .registerSession(BingoSession::class.java)
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore = SampleLocationStore.create()

    @Bean(destroyMethod = "close")
    fun bingoMetricsReporter(registry: MeterRegistry): BingoMetricsReporter =
        BingoMetricsReporter(registry, "session")

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
