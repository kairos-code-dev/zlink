package systems.zlink.e2e.kotlin.spotservice.play

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.SpotRouteResolver
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
import systems.zlink.framework.spots.ZLinkSpotManager
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
        spots: ZLinkSpotManager
    ): EvidenceHttpServer =
        EvidenceHttpServer(
            state,
            json,
            Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"),
            spots
        )

    @Bean
    fun playFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val nodeRid = state.nodeRid()
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.addSpotRemoteAddressResolver(SpotRouteResolver::class.java)
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
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
                        error.spotRid(),
                        error.surface().toString() +
                            "|" + error.errorReason() +
                            "/" + error.errorAction() +
                            "/" + error.packetName()
                    )
                    CompletableFuture.completedFuture(null)
                }
            options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid))
                .addRequestHandler(
                    RoutePingHandler::class.java,
                    Contracts.RoutePingReq::class.java,
                    Contracts.RoutePingRes::class.java,
                    Contracts.ROUTE_PACKET
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
                Contracts.OutboundMsg::class.java,
                "OutboundMsg"
            )
            ingress.addRequestHandler(NoopIngressHandler::class.java, String::class.java, String::class.java, "Noop")
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
            if (streamEndpoint.isNotBlank()) {
                options.addStreamNode("gateway")
                    .bind(streamEndpoint)
                    .registerSession(ScenarioSession::class.java)
                    .addSessionPacketHandler(ActorAuthHandler::class.java)
            }
        }

    @Bean
    fun createOwnedSpot(
        spots: ZLinkSpotManager,
        state: ScenarioState
    ): ApplicationRunner =
        ApplicationRunner {
            val spotRid = if (state.nodeRid() == "play-a") "room-a" else "room-b"
            spots.getOrCreate(UserSpot::class.java, RoutingId.from(spotRid), "bootstrap")
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
