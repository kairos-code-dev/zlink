package systems.zlink.e2e.kotlin.registrymessaging

import java.util.concurrent.CompletableFuture
import org.springframework.boot.ApplicationRunner
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.registrymessaging.provider"],
)
class ProviderApplication {
    @Bean
    fun scenarioState(): ScenarioState {
        val rid = Env.get("ZLINK_KOTLIN_E2E_PROVIDER_RID", "api-a")
        return ScenarioState(rid, Env.get("ZLINK_KOTLIN_E2E_PROVIDER_INSTANCE", rid))
    }

    @Bean
    fun providerFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/${state.providerRid}-flow.log")
                .traceNodeId("kotlin-rm-${state.providerRid}")
                .setMessageDispatchErrorObserver { error ->
                    state.record(
                        "DispatchError",
                        "${error.reason()}/${error.action()}/${error.packetName()}",
                    )
                    CompletableFuture.completedFuture(null)
                }
            options.addHandlersFromPackageOf(ProfileRequestHandler::class.java)
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))

            val apiEndpoint = Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT")
            if (apiEndpoint.isNotBlank()) {
                options.addClientServerChannel(Contracts.API_CHANNEL)
                    .enableServer(apiEndpoint)
                    .serverRoutingId(RoutingId.from(state.providerRid))
                    .addHandlerGroup(Contracts.HANDLER_GROUP)
            }

            val workflowEndpoint = Env.get("ZLINK_KOTLIN_E2E_WORKFLOW_ENDPOINT")
            if (workflowEndpoint.isNotBlank()) {
                options.addClientServerChannel(Contracts.WORKFLOW_CHANNEL)
                    .enableServer(workflowEndpoint)
                    .serverRoutingId(RoutingId.from(state.providerRid))
                    .addHandlerGroup(Contracts.HANDLER_GROUP)
            }

            val routeEndpoint = Env.get("ZLINK_KOTLIN_E2E_ROUTE_ENDPOINT")
            if (routeEndpoint.isNotBlank()) {
                options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                    .enableServer(routeEndpoint)
                    .setRoutingId(RoutingId.from(state.providerRid))
                    .addRequestHandler(
                        RoutePingHandler::class.java,
                        RoutePing::class.java,
                        RoutePong::class.java,
                        Contracts.ROUTE_PACKET,
                    )
            }
        }

    @Bean
    fun applyInitialSocketWeight(runtimeOptions: ZLinkChannelRuntimeOptions): ApplicationRunner =
        ApplicationRunner {
            val weight = Env.get("ZLINK_KOTLIN_E2E_API_WEIGHT")
            if (weight.isNotBlank()) {
                runtimeOptions
                    .clientServerChannel(Contracts.API_CHANNEL)
                    .configureServerSocket()
                    .weight(weight.toInt())
            }
        }

    @Bean
    fun profileRequestHandler(state: ScenarioState): ProfileRequestHandler =
        ProfileRequestHandler(state)

    @Bean
    fun profileCommandHandler(state: ScenarioState): ProfileCommandHandler =
        ProfileCommandHandler(state)

    @Bean
    fun routePingHandler(state: ScenarioState): RoutePingHandler =
        RoutePingHandler(state)
}

fun runProviderApplication(vararg args: String): AutoCloseable {
    val builder = SpringApplicationBuilder(ProviderApplication::class.java)
        .web(WebApplicationType.NONE)
    builder.application().setKeepAlive(true)
    val context = builder.run(*args)
    return AutoCloseable { context.close() }
}
