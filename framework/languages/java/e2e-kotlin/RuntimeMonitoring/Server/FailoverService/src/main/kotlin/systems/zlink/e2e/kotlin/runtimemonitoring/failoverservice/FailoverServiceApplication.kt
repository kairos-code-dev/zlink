package systems.zlink.e2e.kotlin.runtimemonitoring.failoverservice

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.Env
import systems.zlink.e2e.kotlin.runtimemonitoring.service.EvidenceHttpServer
import systems.zlink.e2e.kotlin.runtimemonitoring.service.EvidenceState
import systems.zlink.e2e.kotlin.runtimemonitoring.service.MonitoringEventHandlers
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.runtimemonitoring.failoverservice"],
)
class FailoverServiceApplication {
    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun evidenceState(): EvidenceState = EvidenceState()

    @Bean
    fun evidenceHttpServer(state: EvidenceState, json: ObjectMapper): EvidenceHttpServer {
        return EvidenceHttpServer(state, json, Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"))
    }

    @Bean
    fun frameworkConfigurer(): ZLinkFrameworkConfigurer {
        return ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/failover-service-flow.log")
                .traceLabel("kotlin-mon-failover-service")
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .setRoutingId(RoutingId.from("svc-a"))
                .addRequestHandler(
                    FailoverWorkRequestHandler::class.java,
                    Contracts.WorkReq::class.java,
                    Contracts.WorkRes::class.java,
                    "WorkReq",
                )
        }
    }

    @Bean
    fun monitoringOptions(): ZLinkMonitoringOptionsCustomizer {
        return ZLinkMonitoringOptionsCustomizer { options ->
            options.addSocketEvents(Contracts.CHANNEL)
        }
    }

    @Bean
    fun workRequestHandler(state: EvidenceState): FailoverWorkRequestHandler {
        return FailoverWorkRequestHandler(state)
    }

    @Bean
    fun socketRecorder(state: EvidenceState): MonitoringEventHandlers.SocketRecorder {
        return MonitoringEventHandlers.SocketRecorder(state)
    }

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(FailoverServiceApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
