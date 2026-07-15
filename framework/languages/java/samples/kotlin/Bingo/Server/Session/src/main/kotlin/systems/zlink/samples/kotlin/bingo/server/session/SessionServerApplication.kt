package systems.zlink.samples.kotlin.bingo.server.session

import kotlinx.coroutines.Dispatchers
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.boot.context.properties.EnableConfigurationProperties
import org.springframework.context.annotation.Bean
import org.springframework.core.env.StandardEnvironment
import java.nio.file.Path
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
@EnableConfigurationProperties(SampleTopology::class)
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackageClasses = [SessionServerApplication::class],
)
class SessionServerApplication {
    @Bean
    fun sessionFramework(topology: SampleTopology): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.addHandlersFromPackageOf(SessionServerApplication::class.java)
            options.useCoroutineHandlers(Dispatchers.Default)
            options.configureDispatch {
                messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                traceLogFile(topology.logDirectory + "/flow-session.log")
                traceLabel("session")
            }
            options.codecs().use(ZLinkProtobufCodec.defaultCodec())
            options.configureLocations()
            options.addClientServerChannel(SampleNames.ApiChannel)
                .enableClient()
            val node = options.addSpotMesh(SampleNames.RoomSpotDiscovery)

            node.enableRouter(topology.selectedSessionRouterEndpoint())
                .setRoutingId(RoutingId.from(topology.selectedSessionRouterRid()))
            node.connectRouter(
                RoutingId.from(topology.preferredPlayNodeRid()),
                topology.preferredPlaySpotRouterEndpoint(),
            )
            node.enablePubSub(topology.selectedSessionSpotEndpoint())
            options.addStreamNode(SampleNames.StreamNode)
                .bind(topology.selectedStreamEndpoint())
                .registerSession(BingoSession::class.java)
        }

    @Bean
    fun locationStore(topology: SampleTopology): ZLinkRedisLocationStore = SampleLocationStore.create(topology)

    @Bean(destroyMethod = "close")
    fun bingoMetricsReporter(registry: MeterRegistry): BingoMetricsReporter =
        BingoMetricsReporter(registry, "session")

    companion object {
        fun run(args: Array<String> = emptyArray()): AutoCloseable {
            val environment = StandardEnvironment().apply {
                propertySources.remove(StandardEnvironment.SYSTEM_ENVIRONMENT_PROPERTY_SOURCE_NAME)
                propertySources.remove(StandardEnvironment.SYSTEM_PROPERTIES_PROPERTY_SOURCE_NAME)
            }
            val builder = SpringApplicationBuilder(SessionServerApplication::class.java)
                .environment(environment)
                .properties("spring.config.location=${Path.of(SampleTopology.configPath(args)).toAbsolutePath().toUri()}")
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run()
            return AutoCloseable { context.close() }
        }
    }
}
