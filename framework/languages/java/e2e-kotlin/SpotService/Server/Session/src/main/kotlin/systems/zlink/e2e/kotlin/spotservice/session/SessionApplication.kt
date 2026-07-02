package systems.zlink.e2e.kotlin.spotservice.session

import com.fasterxml.jackson.databind.ObjectMapper
import java.util.concurrent.CompletableFuture
import org.springframework.boot.ApplicationRunner
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.SpotRouteResolver
import systems.zlink.e2e.kotlin.spotservice.session.endpoints.SessionEvidenceHttpServer
import systems.zlink.e2e.kotlin.spotservice.session.handlers.ActorAuthHandler
import systems.zlink.e2e.kotlin.spotservice.session.handlers.MultiBindHandler
import systems.zlink.e2e.kotlin.spotservice.session.handlers.ScenarioSession
import systems.zlink.e2e.kotlin.spotservice.session.handlers.SlowSessionHandler
import systems.zlink.e2e.kotlin.spotservice.session.spots.ScenarioActorFactory
import systems.zlink.e2e.kotlin.spotservice.session.spots.ScenarioEntrySpot
import systems.zlink.e2e.kotlin.spotservice.session.spots.UserSpot
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.session"]
)
class SessionApplication {
    @Bean
    fun scenarioState(): ScenarioState = ScenarioState(Env.get("ZLINK_KOTLIN_E2E_NODE_RID", "session-a"))

    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun evidenceHttpServer(
        state: ScenarioState,
        json: ObjectMapper
    ): SessionEvidenceHttpServer =
        SessionEvidenceHttpServer(
            state,
            json,
            Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT")
        )

    @Bean
    fun sessionFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
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
                        error.spotRid() ?: "",
                        error.surface().toString() +
                            "|" + error.errorReason() +
                            "/" + error.errorAction() +
                            "/" + error.packetName()
                    )
                    CompletableFuture.completedFuture(null)
                }
            options.addSpotMesh(Contracts.SPOT_MESH)
                .enableRouter(Env.get("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid))
                .addEntrySpot(ScenarioEntrySpot::class.java)
                .addSpotFactory(UserSpot::class.java)
                .addActorFactory("scenario", ScenarioActorFactory::class.java)
            options.addStreamNode("gateway")
                .bind(Env.get("ZLINK_KOTLIN_E2E_STREAM_ENDPOINT"))
                .registerSession(ScenarioSession::class.java)
                .addSessionPacketHandler(ActorAuthHandler::class.java)
                .addSessionPacketHandler(MultiBindHandler::class.java)
                .addSessionPacketHandler(SlowSessionHandler::class.java)
        }

    @Bean
    fun createOwnedSpot(
        spots: ZLinkSpotManager
    ): ApplicationRunner =
        ApplicationRunner {
            spots.getOrCreate(UserSpot::class.java, RoutingId.from("room-a"), "bootstrap")
                .toCompletableFuture()
                .join()
        }

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(SessionApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
