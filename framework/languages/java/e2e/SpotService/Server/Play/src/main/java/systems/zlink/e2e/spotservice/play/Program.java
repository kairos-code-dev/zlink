package systems.zlink.e2e.spotservice.play;

import java.nio.file.Path;
import java.time.Duration;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.core.env.StandardEnvironment;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.spotservice.shared.ActorAuthHandler;
import systems.zlink.e2e.spotservice.shared.Contracts;
import systems.zlink.e2e.spotservice.shared.EvidenceHttpServer;
import systems.zlink.e2e.spotservice.shared.IngressMsgHandler;
import systems.zlink.e2e.spotservice.shared.MismatchedSpot;
import systems.zlink.e2e.spotservice.shared.MultiBindHandler;
import systems.zlink.e2e.spotservice.shared.NoopIngressHandler;
import systems.zlink.e2e.spotservice.shared.RouteReqHandler;
import systems.zlink.e2e.spotservice.shared.ScenarioActorFactory;
import systems.zlink.e2e.spotservice.shared.ScenarioEntrySpot;
import systems.zlink.e2e.spotservice.shared.ScenarioSession;
import systems.zlink.e2e.spotservice.shared.ScenarioState;
import systems.zlink.e2e.spotservice.shared.SlowSessionHandler;
import systems.zlink.e2e.spotservice.shared.TimerScenarioSpot;
import systems.zlink.e2e.spotservice.shared.UserSpot;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.configuration.ZLinkMeshNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.ZLinkSpotManager;

@EnableZLinkFramework
@EnableConfigurationProperties(PlayOptions.class)
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotservice.play")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        String config = configPath(args);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .environment(isolatedEnvironment())
            .properties("spring.config.location=" + Path.of(config).toAbsolutePath().toUri())
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run();
    }

    @Bean
    ScenarioState scenarioState(PlayOptions options) {
        return new ScenarioState(options.nodeRid());
    }

    @Bean
    com.fasterxml.jackson.databind.ObjectMapper objectMapper() {
        return new com.fasterxml.jackson.databind.ObjectMapper();
    }

    @Bean
    EvidenceHttpServer evidenceHttpServer(
        ScenarioState state,
        com.fasterxml.jackson.databind.ObjectMapper json,
        ZLinkSpotManager spots,
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        systems.zlink.framework.spots.SpotHandleResolver spotHandles,
        systems.zlink.framework.monitoring.ZLinkRouteMeshRuntime meshRuntime,
        ZLinkRedisLocationStore locationStore,
        PlayOptions options) {
        return new EvidenceHttpServer(
            state,
            json,
            options.httpEndpoint(),
            spots,
            routes,
            spotHandles,
            meshRuntime,
            locationStore);
    }

    @Bean
    ZLinkFrameworkConfigurer playFramework(ScenarioState state, PlayOptions play) {
        return options -> {
            String nodeRid = state.nodeRid();
            String logDir = play.logDir();
            options.configureLocations().setHeartbeatInterval(
                Duration.ofMillis(play.locationHeartbeatMillis()));
            options.configureLocations().setOwnerLeaseTtl(
                Duration.ofMillis(play.locationLeaseTtlMillis()));
            options.addHandlersFromPackageOf(ActorAuthHandler.class);
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + nodeRid + "-flow.log")
                .traceLabel("java-sm-" + nodeRid)
                .setMessageFlowObserver(error -> {
                    if (error.outcome() != ZLinkMessageFlowOutcome.ERROR) {
                        return java.util.concurrent.CompletableFuture.completedFuture(null);
                    }
                    state.record(
                        "DispatchError",
                        error.spotRid(),
                        error.errorReason() + "/" + error.errorAction() + "/" + error.packetName());
                    return java.util.concurrent.CompletableFuture.completedFuture(null);
                });
            ZLinkMeshNodeBuilder node = options.addRouteMesh(Contracts.SPOT_MESH)
                .listen(play.routeEndpoint())
                .setRoutingId(RoutingId.from(nodeRid));
            node.channelName(Contracts.ROUTE_CHANNEL);
            node.addRouteRequestHandler(
                RouteReqHandler.class,
                Contracts.RouteReq.class,
                Contracts.RouteRes.class);
            if (!"play-a".equals(nodeRid)) {
                node.peerConnections().connect(RoutingId.from("play-a"), play.routeAEndpoint());
            }
            if (!"play-b".equals(nodeRid)) {
                node.peerConnections().connect(RoutingId.from("play-b"), play.routeBEndpoint());
            }
            String peerIngress = "play-a".equals(nodeRid)
                ? play.ingressBEndpoint()
                : play.ingressAEndpoint();
            ClientServerChannelBuilder ingress = options.addClientServerChannel(Contracts.INGRESS_CHANNEL)
                .enableServer(play.ingressEndpoint())
                .enableClient(peerIngress)
                .setRoutingId(RoutingId.from(nodeRid));
            ingress.addSendHandler(
                IngressMsgHandler.class,
                Contracts.OutboundMsg.class,
                "OutboundMsg");
            ingress.addRequestHandler(
                NoopIngressHandler.class,
                Contracts.StateReq.class,
                String.class,
                "StateReq");
            node.addEntrySpot(ScenarioEntrySpot.class);
            node.addSpotFactory(UserSpot.class);
            node.addSpotFactory(MismatchedSpot.class);
            node.addSpotFactory(TimerScenarioSpot.class);
            node.addActorFactory("scenario", ScenarioActorFactory.class);
            String streamEndpoint = play.streamEndpoint();
            String tlsStreamEndpoint = play.tlsStreamEndpoint();
            if (!streamEndpoint.isBlank() || !tlsStreamEndpoint.isBlank()) {
                var stream = options.addStreamNode("gateway");
                if (!streamEndpoint.isBlank()) {
                    stream.bind(streamEndpoint);
                }
                if (!tlsStreamEndpoint.isBlank()) {
                    stream.bind(tlsStreamEndpoint)
                        .setTlsServer(
                            play.tlsCertificatePath(),
                            play.tlsKeyPath());
                }
                stream.enableActorDispatch(Contracts.SPOT_MESH);
                stream.registerSession(ScenarioSession.class);
            }
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore(PlayOptions options) {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(options.redisLocationEndpoint())
            .setKeyPrefix(options.locationKeyPrefix()));
    }

    @Bean
    ApplicationRunner createOwnedSpot(
        ZLinkSpotManager spots,
        ScenarioState state) {
        return ignored -> {
            String spotRid = "play-a".equals(state.nodeRid()) ? "room-a" : "room-b";
            spots.getOrCreate(UserSpot.class, RoutingId.from(spotRid), ZLinkMessage.of("bootstrap"))
                .toCompletableFuture()
                .join();
        };
    }

    private static String configPath(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: spot-service-play --config <path>");
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
