package systems.zlink.e2e.kotlin.runtimemonitoring

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.runtimemonitoring.client.ClientScenario
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.runtimemonitoring.client"],
)
class ClientApplication {
    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun clientFramework(): ZLinkFrameworkConfigurer {
        return ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/client-flow.log")
                .traceLabel("kotlin-mon-client")
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_FILTERED_API_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_THROWING_API_ENDPOINT"))
        }
    }

    @Bean
    fun clientScenario(
        client: ZLinkClient,
        json: ObjectMapper,
    ): ClientScenario = ClientScenario(client, json)

    companion object {
        @JvmStatic
        fun run(vararg args: String) {
            val context = SpringApplicationBuilder(ClientApplication::class.java)
                .web(WebApplicationType.NONE)
                .run(*args)
            try {
                context.getBean(ClientScenario::class.java).run()
                println("runtime-monitoring kotlin e2e result=passed")
            } finally {
                context.close()
            }
        }
    }
}
