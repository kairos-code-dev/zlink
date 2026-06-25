package systems.zlink.e2e.kotlin.registrymessaging

import java.time.Duration
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrymessaging.client"],
)
class ClientApplication {
    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceLabel("kotlin-rm-client")
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addClientServerChannel(Contracts.API_CHANNEL).enableClient()
            options.addClientServerChannel(Contracts.WORKFLOW_CHANNEL).enableClient()
            options.addClientServerChannel("registry.messaging.kotlin.api.manual")
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_API_A_ENDPOINT"))
            options.addClientServerChannel("registry.messaging.kotlin.api.manual.multi")
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_API_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_API_B_ENDPOINT"))
            options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_CLIENT_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from("client"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_ROUTE_B_ENDPOINT"))
                .setDefaultRequestTimeout(Duration.ofSeconds(2))
        }

    @Bean
    fun clientScenario(client: ZLinkClient, routes: ZLinkRouteClient): ClientScenario =
        ClientScenario(client, routes)
}

fun runClientApplication(vararg args: String) {
    val context = SpringApplicationBuilder(ClientApplication::class.java)
        .web(WebApplicationType.NONE)
        .run(*args)
    try {
        context.getBean(ClientScenario::class.java).run()
        println("registry-messaging kotlin e2e result=passed")
    } finally {
        context.close()
    }
}
