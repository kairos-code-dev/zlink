package systems.zlink.e2e.kotlin.runtimemonitoring.service

import com.fasterxml.jackson.databind.ObjectMapper
import org.springframework.beans.factory.ObjectProvider
import org.springframework.boot.ApplicationRunner
import org.springframework.boot.WebApplicationType
import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.builder.SpringApplicationBuilder
import org.springframework.context.annotation.Bean
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.Env
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode
import systems.zlink.framework.monitoring.ZLinkSocketEventKind
import systems.zlink.framework.spring.EnableZLinkFramework
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer
import systems.zlink.framework.spring.ZLinkMonitoringLifecycle
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer
import systems.zlink.framework.spots.ZLinkSpotManager
import java.time.Duration

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
class ServiceApplication {
    @Bean
    fun objectMapper(): ObjectMapper = ObjectMapper()

    @Bean
    fun evidenceState(): EvidenceState = EvidenceState()

    @Bean
    fun evidenceHttpServer(
        state: EvidenceState,
        json: ObjectMapper,
        runtimeOptions: ZLinkChannelRuntimeOptions,
    ): EvidenceHttpServer {
        return EvidenceHttpServer(
            state,
            json,
            Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"),
            runtimeOptions,
        )
    }

    @Bean
    fun frameworkConfigurer(): ZLinkFrameworkConfigurer {
        return ZLinkFrameworkConfigurer { options ->
            val logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs")
            options.codecs().addJson()
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"))
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile("$logDir/service-flow.log")
                .traceLabel("kotlin-mon-service")
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .setRoutingId(RoutingId.from("svc-a"))
                .addRequestHandler(
                    WorkRequestHandler::class.java,
                    Contracts.WorkRequest::class.java,
                    Contracts.WorkReply::class.java,
                    "WorkRequest",
                )
            options.addClientServerChannel(Contracts.HANDSHAKE_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_HANDSHAKE_ENDPOINT"))
                .setRoutingId(RoutingId.from("svc-a-handshake"))
                .addRequestHandler(
                    WorkRequestHandler::class.java,
                    Contracts.WorkRequest::class.java,
                    Contracts.WorkReply::class.java,
                    "HandshakeWorkRequest",
                )
            val node = options.addSpotMesh(Contracts.SPOT_MESH)
            node.enableRouter(Env.get("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from("svc-a-spot"))
            node.enablePubSub(Env.get("ZLINK_KOTLIN_E2E_SPOT_PUB_ENDPOINT"))
            node.addSpotFactory(MonitoringSpot::class.java)
        }
    }

    @Bean
    fun monitoringOptions(): ZLinkMonitoringOptionsCustomizer {
        return ZLinkMonitoringOptionsCustomizer { options ->
            options.addSocketEvents(Contracts.CHANNEL, ZLinkSocketEventKind.CONNECTION_READY)
            options.addSocketEvents(Contracts.HANDSHAKE_CHANNEL)
            options.addSpotEvents(Contracts.SPOT_MESH, Duration.ofMillis(100))
        }
    }

    @Bean
    fun workRequestHandler(): WorkRequestHandler = WorkRequestHandler()

    @Bean
    fun createSpot(spots: ZLinkSpotManager): ApplicationRunner {
        return ApplicationRunner {
            spots.getOrCreate(
                MonitoringSpot::class.java,
                RoutingId.from("monitoring-room"),
                "bootstrap",
            ).toCompletableFuture().join()
        }
    }

    @Bean
    fun recordMonitoringLifecycle(
        lifecycle: ObjectProvider<ZLinkMonitoringLifecycle>,
        state: EvidenceState,
    ): ApplicationRunner {
        return ApplicationRunner {
            state.record(
                "system",
                "service",
                "MonitoringLifecycle",
                "running=${lifecycle.stream().anyMatch { it.isRunning }}",
            )
        }
    }

    @Bean
    fun socketRecorder(state: EvidenceState): MonitoringEventHandlers.SocketRecorder {
        return MonitoringEventHandlers.SocketRecorder(state)
    }

    @Bean
    fun spotRecorder(state: EvidenceState): MonitoringEventHandlers.SpotRecorder {
        return MonitoringEventHandlers.SpotRecorder(state)
    }

    companion object {
        @JvmStatic
        fun run(vararg args: String): AutoCloseable {
            val builder = SpringApplicationBuilder(ServiceApplication::class.java)
                .web(WebApplicationType.NONE)
            builder.application().setKeepAlive(true)
            val context = builder.run(*args)
            return AutoCloseable { context.close() }
        }
    }
}
