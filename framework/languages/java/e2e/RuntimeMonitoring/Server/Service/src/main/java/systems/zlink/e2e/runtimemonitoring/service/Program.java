package systems.zlink.e2e.runtimemonitoring.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.runtimemonitoring.service.handlers.MonitoringEventHandlers;
import systems.zlink.e2e.runtimemonitoring.service.handlers.MonitoringSpot;
import systems.zlink.e2e.runtimemonitoring.service.handlers.TriggeredMonitoringSpot;
import systems.zlink.e2e.runtimemonitoring.service.handlers.WorkReqHandler;
import systems.zlink.e2e.runtimemonitoring.service.support.EvidenceHttpServer;
import systems.zlink.e2e.runtimemonitoring.service.support.EvidenceState;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.e2e.runtimemonitoring.shared.Env;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind;
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
    EvidenceHttpServer evidenceHttpServer(
        EvidenceState state,
        ObjectMapper json,
        systems.zlink.framework.channels.ZLinkChannelRuntimeOptions runtimeOptions,
        ObjectProvider<ZLinkSpotManager> spots,
        org.springframework.context.ConfigurableApplicationContext applicationContext) {
        return new EvidenceHttpServer(
            state,
            json,
            runtimeOptions,
            spots,
            applicationContext,
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkFrameworkConfigurer frameworkConfigurer() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureLocations().setHeartbeatInterval(Duration.ofMillis(500));
            options.configureLocations().setOwnerLeaseTtl(Duration.ofSeconds(3));
            options.configureLocations().setPollingInterval(Duration.ofMillis(250));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/service-flow.log")
                .traceLabel("java-mon-service");
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"))
                .setRoutingId(RoutingId.from(Env.get("ZLINK_JAVA_E2E_RID", "svc-a")))
                .addRequestHandler(
                    WorkReqHandler.class,
                    Contracts.WorkReq.class,
                    Contracts.WorkRes.class,
                    "WorkReq");
            if (enabled("ZLINK_JAVA_E2E_ENABLE_HANDSHAKE", true)) {
                options.addClientServerChannel(Contracts.HANDSHAKE_CHANNEL)
                    .enableServer(Env.get("ZLINK_JAVA_E2E_HANDSHAKE_ENDPOINT"))
                    .setRoutingId(RoutingId.from(Env.get("ZLINK_JAVA_E2E_RID", "svc-a") + "-handshake"))
                    .addRequestHandler(
                        WorkReqHandler.class,
                        Contracts.WorkReq.class,
                        Contracts.WorkRes.class,
                        "HandshakeWorkReq");
            }
            if (enabled("ZLINK_JAVA_E2E_ENABLE_SPOT", true)) {
                ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH);
                node.enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                    .setRoutingId(RoutingId.from(Env.get("ZLINK_JAVA_E2E_RID", "svc-a") + "-spot"));
                node.enablePubSub(Env.get("ZLINK_JAVA_E2E_SPOT_PUB_ENDPOINT"));
                node.addSpotFactory(MonitoringSpot.class);
                node.addSpotFactory(TriggeredMonitoringSpot.class);
            }
        };
    }

    @Bean
    ZLinkMonitoringOptionsCustomizer monitoringOptions() {
        return options -> {
            options.addSocketEvents(
                Contracts.CHANNEL,
                ZLinkSocketEventKind.CONNECTION_READY,
                ZLinkSocketEventKind.PEER_ADMISSION_CHANGED);
            options.addLocationRuntimeEvents(Contracts.LOCATION_SOURCE, Duration.ofMillis(100));
            if (enabled("ZLINK_JAVA_E2E_ENABLE_HANDSHAKE", true)) {
                options.addSocketEvents(Contracts.HANDSHAKE_CHANNEL);
            }
            if (enabled("ZLINK_JAVA_E2E_ENABLE_SPOT", true)) {
                options.addSpotEvents(Contracts.SPOT_MESH, Duration.ofMillis(100));
            }
        };
    }

    @Bean
    WorkReqHandler workRequestHandler(EvidenceState state) {
        return new WorkReqHandler(state);
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX"))
            .setCommandTimeout(Duration.ofMillis(500)));
    }

    @Bean
    ApplicationRunner createSpot(ObjectProvider<ZLinkSpotManager> spots) {
        return ignored -> {
            if (!enabled("ZLINK_JAVA_E2E_ENABLE_SPOT", true)) {
                return;
            }
            ZLinkSpotManager manager = spots.getIfAvailable();
            if (manager == null) {
                throw new IllegalStateException("spot manager is required when spot monitoring is enabled");
            }
            manager.getOrCreate(
                MonitoringSpot.class,
                RoutingId.from("monitoring-room"),
                ZLinkMessage.of("bootstrap"))
                .whenComplete((ignoredResult, error) -> {
                    if (error != null) {
                        throw new IllegalStateException("failed to create monitoring Spot", error);
                    }
                });
        };
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

    @Bean
    MonitoringEventHandlers.LocationRuntimeRecorder locationRuntimeRecorder(EvidenceState state) {
        return new MonitoringEventHandlers.LocationRuntimeRecorder(state);
    }

    private static boolean enabled(String name, boolean fallback) {
        return Boolean.parseBoolean(Env.get(name, Boolean.toString(fallback)));
    }
}
