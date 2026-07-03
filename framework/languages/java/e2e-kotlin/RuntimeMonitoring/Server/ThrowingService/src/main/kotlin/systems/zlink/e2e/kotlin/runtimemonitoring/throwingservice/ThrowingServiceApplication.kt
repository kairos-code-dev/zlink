package systems.zlink.e2e.kotlin.runtimemonitoring.throwingservice

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
import systems.zlink.e2e.kotlin.runtimemonitoring.service.WorkRequestHandler
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer
import java.time.Duration

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = ["systems.zlink.e2e.kotlin.runtimemonitoring.throwingservice"],
)
class ThrowingServiceApplication {
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
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/throwing-service-flow.log")
                .traceLabel("kotlin-mon-throwing-service")
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .setRoutingId(RoutingId.from("svc-throw"))
                .addRequestHandler(
                    WorkRequestHandler::class.java,
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
    fun workRequestHandler(): WorkRequestHandler = WorkRequestHandler()

    @Bean
    fun locationStore(): ZLinkRedisLocationStore {
        return ZLinkRedisLocationStore(
            ZLinkRedisLocationOptions()
                .setConnectionString(Env.get("ZLINK_KOTLIN_E2E_REDIS_LOCATION_ENDPOINT"))
                .setKeyPrefix(Env.get("ZLINK_KOTLIN_E2E_LOCATION_KEY_PREFIX"))
                .setCommandTimeout(Duration.ofMillis(500)),
        )
    }

    @Bean
    fun socketRecorder(state: EvidenceState): MonitoringEventHandlers.SocketRecorder {
        return MonitoringEventHandlers.SocketRecorder(state)
    }

    @Bean
    fun failingSocketRecorder(state: EvidenceState): MonitoringEventHandlers.FailingSocketRecorder {
        return MonitoringEventHandlers.FailingSocketRecorder(state)
    }

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ThrowingServiceApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
