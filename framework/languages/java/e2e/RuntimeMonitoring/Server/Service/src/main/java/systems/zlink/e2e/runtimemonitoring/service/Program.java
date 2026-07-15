package systems.zlink.e2e.runtimemonitoring.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.nio.file.Path;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.context.annotation.Bean;
import org.springframework.core.env.StandardEnvironment;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.runtimemonitoring.service.handlers.MonitoringEventHandlers;
import systems.zlink.e2e.runtimemonitoring.service.handlers.MonitoringSpot;
import systems.zlink.e2e.runtimemonitoring.service.handlers.TriggeredMonitoringSpot;
import systems.zlink.e2e.runtimemonitoring.service.handlers.WorkReqHandler;
import systems.zlink.e2e.runtimemonitoring.service.support.EvidenceHttpServer;
import systems.zlink.e2e.runtimemonitoring.service.support.EvidenceState;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
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
@EnableConfigurationProperties(ServiceOptions.class)
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.runtimemonitoring.service")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        run(args);
    }

    public static void run(String... args) {
        String config = configPath(args);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .environment(isolatedEnvironment())
            .properties("spring.config.location=" + Path.of(config).toAbsolutePath().toUri())
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run();
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceState evidenceState(ServiceOptions config) {
        return new EvidenceState(config.routingId());
    }

    @Bean
    EvidenceHttpServer evidenceHttpServer(
        EvidenceState state,
        ObjectMapper json,
        systems.zlink.framework.channels.ZLinkChannelRuntimeOptions runtimeOptions,
        ObjectProvider<ZLinkSpotManager> spots,
        org.springframework.context.ConfigurableApplicationContext applicationContext,
        ServiceOptions config) {
        return new EvidenceHttpServer(
            state,
            json,
            runtimeOptions,
            spots,
            applicationContext,
            config.httpEndpoint());
    }

    @Bean
    ZLinkFrameworkConfigurer frameworkConfigurer(ServiceOptions config) {
        return options -> {
            options.configureLocations().setHeartbeatInterval(Duration.ofMillis(500));
            options.configureLocations().setOwnerLeaseTtl(Duration.ofSeconds(3));
            options.configureLocations().setPollingInterval(Duration.ofMillis(250));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(config.logDirectory() + "/service-flow.log")
                .traceLabel("java-mon-service");
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(config.apiEndpoint())
                .setRoutingId(RoutingId.from(config.routingId()))
                .addRequestHandler(
                    WorkReqHandler.class,
                    Contracts.WorkReq.class,
                    Contracts.WorkRes.class,
                    "WorkReq");
            if (config.enableHandshake()) {
                options.addClientServerChannel(Contracts.HANDSHAKE_CHANNEL)
                    .enableServer(config.handshakeEndpoint())
                    .setRoutingId(RoutingId.from(config.routingId() + "-handshake"))
                    .addRequestHandler(
                        WorkReqHandler.class,
                        Contracts.WorkReq.class,
                        Contracts.WorkRes.class,
                        "HandshakeWorkReq");
            }
            if (config.enableSpot()) {
                ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH);
                node.enableRouter(config.spotEndpoint())
                    .setRoutingId(RoutingId.from(config.routingId() + "-spot"));
                node.enablePubSub(config.spotPubEndpoint());
                node.addSpotFactory(MonitoringSpot.class);
                node.addSpotFactory(TriggeredMonitoringSpot.class);
            }
        };
    }

    @Bean
    ZLinkMonitoringOptionsCustomizer monitoringOptions(ServiceOptions config) {
        return options -> {
            options.addSocketEvents(
                Contracts.CHANNEL,
                ZLinkSocketEventKind.CONNECTION_READY,
                ZLinkSocketEventKind.PEER_ADMISSION_CHANGED);
            options.addLocationRuntimeEvents(Contracts.LOCATION_SOURCE, Duration.ofMillis(100));
            if (config.enableHandshake()) {
                options.addSocketEvents(Contracts.HANDSHAKE_CHANNEL);
            }
            if (config.enableSpot()) {
                options.addSpotEvents(Contracts.SPOT_MESH, Duration.ofMillis(100));
            }
        };
    }

    @Bean
    WorkReqHandler workRequestHandler(EvidenceState state, ServiceOptions config) {
        return new WorkReqHandler(state, config.routingId());
    }

    @Bean
    ZLinkRedisLocationStore locationStore(ServiceOptions config) {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(config.redisLocationEndpoint())
            .setKeyPrefix(config.locationKeyPrefix())
            .setCommandTimeout(Duration.ofMillis(500)));
    }

    @Bean
    ApplicationRunner createSpot(ObjectProvider<ZLinkSpotManager> spots, ServiceOptions config) {
        return ignored -> {
            if (!config.enableSpot()) {
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

    private static String configPath(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: runtime-monitoring-service --config <path>");
        }
        return args[1];
    }

    private static StandardEnvironment isolatedEnvironment() {
        StandardEnvironment value = new StandardEnvironment();
        value.getPropertySources().remove(StandardEnvironment.SYSTEM_ENVIRONMENT_PROPERTY_SOURCE_NAME);
        value.getPropertySources().remove(StandardEnvironment.SYSTEM_PROPERTIES_PROPERTY_SOURCE_NAME);
        return value;
    }
}
