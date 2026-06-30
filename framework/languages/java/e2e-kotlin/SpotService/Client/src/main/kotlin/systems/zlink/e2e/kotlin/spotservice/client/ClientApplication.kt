package systems.zlink.e2e.kotlin.spotservice.client

import java.util.UUID
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.SpotRouteResolver
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.spotservice.client"],
)
class ClientApplication {
    @Bean
    fun scenarioState(): ScenarioState =
        ScenarioState("client")

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            val clientRid = "client-${Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "state1")}"
            options.codecs().addJson()
            options.addSpotRemoteAddressResolver(SpotRouteResolver::class.java)
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceLabel("kotlin-sm-client")
            options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT"))
                .setRoutingId(RoutingId.from(clientRid))
            options.addClientServerChannel(Contracts.EGRESS_CHANNEL)
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_INGRESS_A_ENDPOINT"))
            val node: ZLinkSpotNodeBuilder = options.addSpotMesh(Contracts.SPOT_MESH)
            node.enableRouter(Env.get("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(clientRid))
            node.addSpotFactory(ClientDriverSpot::class.java)
        }
}

fun runClientApplication(vararg args: String) {
    val context = SpringApplicationBuilder(ClientApplication::class.java)
        .web(WebApplicationType.NONE)
        .run(*args)
    try {
        val spots = context.getBean(ZLinkSpotManager::class.java)
        val mode = Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "state1")
        ClientDriverSpot.configure(mode)
        spots.create(
            ClientDriverSpot::class.java,
            RoutingId.from("client-driver-$mode-${UUID.randomUUID().toString().replace("-", "")}"),
        ).toCompletableFuture().join()
        ClientDriverSpot.awaitResult()
        println("spot-service kotlin e2e mode=$mode result=passed")
        System.exit(0)
    } finally {
        context.close()
    }
}
