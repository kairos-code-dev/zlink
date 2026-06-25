package systems.zlink.e2e.kotlin.pubsub

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.pubsub.client"],
)
class ClientApplication {
    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceNodeId("kotlin-ps-client")
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.addFanoutChannel(Contracts.EVENT_CHANNEL)
                .enablePublisher(Env.get("ZLINK_KOTLIN_E2E_PUBLISHER_ENDPOINT"))
        }

    @Bean
    fun clientScenario(
        fanout: ZLinkFanoutClient,
        json: ObjectMapper,
    ): ClientScenario =
        ClientScenario(fanout, json)
}

fun runClientApplication(vararg args: String) {
    val context = SpringApplicationBuilder(ClientApplication::class.java)
        .web(WebApplicationType.NONE)
        .run(*args)
    try {
        context.getBean(ClientScenario::class.java).run()
        println("pub-sub kotlin e2e result=passed")
    } finally {
        context.close()
    }
}
