package systems.zlink.e2e.kotlin.spotservice.play

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.endpoints.EvidenceHttpServer
import systems.zlink.e2e.kotlin.spotservice.play.handlers.*
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import com.fasterxml.jackson.databind.ObjectMapper
import java.util.concurrent.CompletableFuture
import org.springframework.boot.ApplicationRunner
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.configuration.ClientServerChannelBuilder
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spots.SpotHandleResolver
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.play"]
)
class PlayApplication {
    @Bean
    fun scenarioState(): ScenarioState = ScenarioState(Env.get("ZLINK_KOTLIN_E2E_NODE_RID", "play-a"))

    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun evidenceHttpServer(
        state: ScenarioState,
        json: ObjectMapper,
        spots: ZLinkSpotManager,
        routes: ZLinkRouteClient,
        handles: SpotHandleResolver,
    ): EvidenceHttpServer =
        EvidenceHttpServer(
            state,
            json,
            Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"),
            spots,
            routes,
            handles,
        )

    @Bean
    fun playFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val nodeRid = state.nodeRid()
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/$nodeRid-flow.log")
                .traceLabel("kotlin-sm-$nodeRid")
                .setMessageFlowObserver { error ->
                    if (error.outcome() != ZLinkMessageFlowOutcome.ERROR) {
                        return@setMessageFlowObserver CompletableFuture.completedFuture(null)
                    }
                    state.record(
                        "DispatchError",
                        error.spotRid() ?: "",
                        error.surface().toString() +
                            "|" + error.errorReason() +
                            "/" + error.errorAction() +
                            "/" + error.packetName()
                    )
                    CompletableFuture.completedFuture(null)
                }
            val route = options.addRouteMeshChannel(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid))
            route.addRequestHandler(
                RoutePingHandler::class.java,
                Contracts.RoutePingReq::class.java,
                Contracts.RoutePingRes::class.java,
                Contracts.ROUTE_PACKET
            )
            route.addRequestHandler(
                EnsureActorHandler::class.java,
                Contracts.EnsureActorReq::class.java,
                Contracts.EnsureActorRes::class.java,
                "EnsureActorReq"
            )
            val peerIngress = if (nodeRid == "play-a") {
                Env.get("ZLINK_KOTLIN_E2E_INGRESS_B_ENDPOINT")
            } else {
                Env.get("ZLINK_KOTLIN_E2E_INGRESS_A_ENDPOINT")
            }
            val ingress: ClientServerChannelBuilder = options.addClientServerChannel(Contracts.INGRESS_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_INGRESS_ENDPOINT"))
                .enableClient(peerIngress)
                .setRoutingId(RoutingId.from(nodeRid))
            ingress.addSendHandler(
                IngressCommandHandler::class.java,
                Contracts.OutboundMsg::class.java
            )
            ingress.addRequestHandler(NoopIngressHandler::class.java, String::class.java, String::class.java)
            val node: ZLinkSpotNodeBuilder = options.addSpotMesh(Contracts.SPOT_MESH)
            node.enableRouter(Env.get("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .enablePubSub(Env.get("ZLINK_KOTLIN_E2E_SPOT_PUB_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid))
            node.addEntrySpot(ScenarioEntrySpot::class.java)
            node.addSpotFactory(UserSpot::class.java)
            node.addSpotFactory(MismatchedSpot::class.java)
            node.addSpotFactory(TimerScenarioSpot::class.java)
            node.addActorFactory("scenario", ScenarioActorFactory::class.java)
            val streamEndpoint = Env.get("ZLINK_KOTLIN_E2E_STREAM_ENDPOINT")
            val tlsStreamEndpoint = Env.get("ZLINK_KOTLIN_E2E_TLS_STREAM_ENDPOINT", "")
            if (streamEndpoint.isNotBlank() || tlsStreamEndpoint.isNotBlank()) {
                val stream = options.addStreamNode("gateway")
                if (streamEndpoint.isNotBlank()) {
                    stream.bind(streamEndpoint)
                }
                if (tlsStreamEndpoint.isNotBlank()) {
                    stream.bind(tlsStreamEndpoint)
                        .setTlsServer(
                            Env.get("ZLINK_KOTLIN_E2E_TLS_CERTIFICATE_PATH"),
                            Env.get("ZLINK_KOTLIN_E2E_TLS_KEY_PATH")
                        )
                }
                stream.registerSession(ScenarioSession::class.java)
                    .addSessionPacketHandler(ActorAuthHandler::class.java)
            }
        }

    @Bean
    fun locationStore(): ZLinkRedisLocationStore =
        ZLinkRedisLocationStore(
            ZLinkRedisLocationOptions()
                .setConnectionString(Env.get("ZLINK_KOTLIN_E2E_REDIS_LOCATION_ENDPOINT"))
                .setKeyPrefix(Env.get("ZLINK_KOTLIN_E2E_LOCATION_KEY_PREFIX"))
        )

    @Bean
    fun createOwnedSpot(
        spots: ZLinkSpotManager,
        state: ScenarioState
    ): ApplicationRunner =
        ApplicationRunner {
            val spotRid = if (state.nodeRid() == "play-a") "room-a" else "room-b"
            spots.getOrCreate(UserSpot::class.java, RoutingId.from(spotRid), ZLinkMessage.of("bootstrap"))
                .toCompletableFuture()
                .join()
        }

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(PlayApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
