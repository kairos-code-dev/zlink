package systems.zlink.e2e.kotlin.monitoring;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spring.ZLinkMonitoringLifecycle;
import systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer;
import systems.zlink.framework.spots.ZLinkSpotManager;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.kotlin.monitoring.handlers")
public final class ServiceApplication {
    private ServiceApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(ServiceApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceState evidenceState() {
        return new EvidenceState();
    }

    @Bean
    EvidenceHttpServer evidenceHttpServer(EvidenceState state, ObjectMapper json) {
        return new EvidenceHttpServer(state, json, Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkFrameworkConfigurer frameworkConfigurer() {
        return options -> {
            String logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/service-flow.log")
                .traceNodeId("kotlin-mon-service");
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_API_ENDPOINT"))
                .serverRoutingId(RoutingId.from("svc-a"))
                .addRequestHandler(
                    WorkRequestHandler.class,
                    Contracts.WorkRequest.class,
                    Contracts.WorkReply.class,
                    "WorkRequest");
            options.addClientServerChannel(Contracts.HANDSHAKE_CHANNEL)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_HANDSHAKE_ENDPOINT"))
                .serverRoutingId(RoutingId.from("svc-a-handshake"))
                .addRequestHandler(
                    WorkRequestHandler.class,
                    Contracts.WorkRequest.class,
                    Contracts.WorkReply.class,
                    "HandshakeWorkRequest");
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH)
                ;
            node.enableRouter(Env.get("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRouterRoutingId(RoutingId.from("svc-a-spot"));
            node.enablePubSub(Env.get("ZLINK_KOTLIN_E2E_SPOT_PUB_ENDPOINT"))
                .setPubSubRoutingId(RoutingId.from("svc-a-spot-pub"));
            node.addSpotFactory(MonitoringSpot.class);
        };
    }

    @Bean
    ZLinkMonitoringOptionsCustomizer monitoringOptions() {
        return options -> {
            options.addSocketEvents(Contracts.CHANNEL, ZLinkSocketEventKind.CONNECTION_READY);
            options.addSocketEvents(Contracts.HANDSHAKE_CHANNEL);
            options.addSpotEvents(Contracts.SPOT_MESH, Duration.ofMillis(100));
        };
    }

    @Bean
    WorkRequestHandler workRequestHandler() {
        return new WorkRequestHandler();
    }

    @Bean
    ApplicationRunner createSpot(ZLinkSpotManager spots) {
        return ignored -> spots.getOrCreate(
                MonitoringSpot.class,
                RoutingId.from("monitoring-room"),
                "bootstrap")
            .toCompletableFuture()
            .join();
    }

    @Bean
    ApplicationRunner recordMonitoringLifecycle(
        org.springframework.beans.factory.ObjectProvider<ZLinkMonitoringLifecycle> lifecycle,
        EvidenceState state) {
        return ignored -> state.record(
            "system",
            "service",
            "MonitoringLifecycle",
            "running=" + lifecycle.stream().anyMatch(ZLinkMonitoringLifecycle::isRunning));
    }

    @Bean
    MonitoringEventHandlers.SocketRecorder socketRecorder(EvidenceState state) {
        return new MonitoringEventHandlers.SocketRecorder(state);
    }

    @Bean
    MonitoringEventHandlers.FailingSocketRecorder failingSocketRecorder(EvidenceState state) {
        return new MonitoringEventHandlers.FailingSocketRecorder(state);
    }

    @Bean
    MonitoringEventHandlers.SpotRecorder spotRecorder(EvidenceState state) {
        return new MonitoringEventHandlers.SpotRecorder(state);
    }
}
