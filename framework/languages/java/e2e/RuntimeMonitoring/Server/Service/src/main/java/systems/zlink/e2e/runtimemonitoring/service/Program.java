package systems.zlink.e2e.runtimemonitoring.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.runtimemonitoring.service.handlers.MonitoringEventHandlers;
import systems.zlink.e2e.runtimemonitoring.service.handlers.MonitoringSpot;
import systems.zlink.e2e.runtimemonitoring.service.handlers.WorkReqHandler;
import systems.zlink.e2e.runtimemonitoring.service.support.EvidenceHttpServer;
import systems.zlink.e2e.runtimemonitoring.service.support.EvidenceState;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.e2e.runtimemonitoring.shared.Env;
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
    scanBasePackages = "systems.zlink.e2e.runtimemonitoring.service")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run(args);
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
        return new EvidenceHttpServer(state, json, Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkFrameworkConfigurer frameworkConfigurer() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/service-flow.log")
                .traceLabel("java-mon-service");
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"))
                .setRoutingId(RoutingId.from("svc-a"))
                .addRequestHandler(
                    WorkReqHandler.class,
                    Contracts.WorkReq.class,
                    Contracts.WorkRes.class,
                    "WorkReq");
            options.addClientServerChannel(Contracts.HANDSHAKE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_HANDSHAKE_ENDPOINT"))
                .setRoutingId(RoutingId.from("svc-a-handshake"))
                .addRequestHandler(
                    WorkReqHandler.class,
                    Contracts.WorkReq.class,
                    Contracts.WorkRes.class,
                    "HandshakeWorkReq");
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH)
                ;
            node.enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from("svc-a-spot"));
            node.enablePubSub(Env.get("ZLINK_JAVA_E2E_SPOT_PUB_ENDPOINT"));
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
    WorkReqHandler workRequestHandler() {
        return new WorkReqHandler();
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
