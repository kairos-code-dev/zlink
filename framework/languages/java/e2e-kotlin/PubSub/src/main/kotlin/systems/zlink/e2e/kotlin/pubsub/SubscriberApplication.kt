package systems.zlink.e2e.kotlin.pubsub

import com.fasterxml.jackson.databind.ObjectMapper
import java.util.concurrent.CompletableFuture
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.pubsub.subscriber"],
)
class SubscriberApplication {
    @Bean
    fun scenarioState(): ScenarioState =
        ScenarioState(
            Env.get("ZLINK_KOTLIN_E2E_SUBSCRIBER_RID", "sub-1"),
            ScenarioState.topicsFromEnv(),
        )

    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun evidenceHttpServer(
        state: ScenarioState,
        json: ObjectMapper,
    ): EvidenceHttpServer =
        EvidenceHttpServer(state, json, Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))

    @Bean
    fun subscriberFramework(state: ScenarioState): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/${state.subscriberRid}-flow.log")
                .traceLabel("kotlin-ps-${state.subscriberRid}")
                .setMessageFlowObserver { error ->
                    if (error.outcome() != ZLinkMessageFlowOutcome.ERROR) {
                        return@setMessageFlowObserver CompletableFuture.completedFuture(null)
                    }
                    state.record(
                        "DispatchError",
                        error.topic(),
                        "observer",
                        -1,
                        "${error.errorReason()}/${error.errorAction()}/${error.packetName()}",
                    )
                    CompletableFuture.completedFuture(null)
                }
            options.addHandlersFromPackageOf(EventNotifyHandler::class.java)
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addFanoutChannel(Contracts.EVENT_CHANNEL)
                .enableSubscriber()
                .addHandlerGroup(Contracts.HANDLER_GROUP)
        }

    @Bean
    fun eventNotifyHandler(state: ScenarioState): EventNotifyHandler =
        EventNotifyHandler(state)
}

fun runSubscriberApplication(vararg args: String): AutoCloseable {
    val builder = SpringApplicationBuilder(SubscriberApplication::class.java)
        .web(WebApplicationType.NONE)
    builder.application().setKeepAlive(true)
    val context = builder.run(*args)
    return AutoCloseable { context.close() }
}
