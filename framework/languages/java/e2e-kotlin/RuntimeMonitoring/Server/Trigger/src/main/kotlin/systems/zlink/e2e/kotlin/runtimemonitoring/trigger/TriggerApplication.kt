package systems.zlink.e2e.kotlin.runtimemonitoring.trigger

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.SpringBootConfiguration
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.EnableAutoConfiguration
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.Env
import systems.zlink.e2e.kotlin.runtimemonitoring.service.EvidenceState
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer

@EnableZLinkFramework
@SpringBootConfiguration(proxyBeanMethods = false)
@EnableAutoConfiguration
class TriggerApplication {
    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun evidenceState(): EvidenceState = EvidenceState()

    @Bean
    fun triggerHttpServer(
        state: EvidenceState,
        json: ObjectMapper,
        client: ZLinkClient,
    ): TriggerHttpServer {
        return TriggerHttpServer(
            Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"),
            state,
            json,
            client,
        )
    }

    @Bean
    fun frameworkConfigurer(): ZLinkFrameworkConfigurer {
        return ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/trigger-flow.log")
                .traceLabel("kotlin-mon-trigger")
            options.addClientServerChannel(Contracts.CHANNEL)
                .client()
                .connect(Env.get("ZLINK_KOTLIN_E2E_SERVICE_API_ENDPOINT"))
        }
    }

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(TriggerApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
