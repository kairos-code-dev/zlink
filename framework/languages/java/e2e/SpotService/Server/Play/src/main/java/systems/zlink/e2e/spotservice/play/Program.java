package systems.zlink.e2e.spotservice.play;

import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.spotservice.shared.ActorAuthHandler;
import systems.zlink.e2e.spotservice.shared.Contracts;
import systems.zlink.e2e.spotservice.shared.Env;
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
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.ZLinkSpotManager;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotservice.play")
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
    ScenarioState scenarioState() {
        return new ScenarioState(Env.get("ZLINK_JAVA_E2E_NODE_RID", "play-a"));
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
        systems.zlink.framework.spots.SpotHandleResolver spotHandles) {
        return new EvidenceHttpServer(
            state,
            json,
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            spots,
            routes,
            spotHandles);
    }

    @Bean
    ZLinkFrameworkConfigurer playFramework(ScenarioState state) {
        return options -> {
            String nodeRid = state.nodeRid();
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
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
            options.addRouteMeshChannel(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid))
                .addRequestHandler(
                    RouteReqHandler.class,
                    Contracts.RouteReq.class,
                    Contracts.RouteRes.class,
                    Contracts.ROUTE_PACKET);
            String peerIngress = "play-a".equals(nodeRid)
                ? Env.get("ZLINK_JAVA_E2E_INGRESS_B_ENDPOINT")
                : Env.get("ZLINK_JAVA_E2E_INGRESS_A_ENDPOINT");
            ClientServerChannelBuilder ingress = options.addClientServerChannel(Contracts.INGRESS_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_INGRESS_ENDPOINT"))
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
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH);
            node.enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                .enablePubSub(Env.get("ZLINK_JAVA_E2E_SPOT_PUB_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            node.addEntrySpot(ScenarioEntrySpot.class);
            node.addSpotFactory(UserSpot.class);
            node.addSpotFactory(MismatchedSpot.class);
            node.addSpotFactory(TimerScenarioSpot.class);
            node.addActorFactory("scenario", ScenarioActorFactory.class);
            String streamEndpoint = Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT");
            String tlsStreamEndpoint = Env.get("ZLINK_JAVA_E2E_TLS_STREAM_ENDPOINT", "");
            if (!streamEndpoint.isBlank() || !tlsStreamEndpoint.isBlank()) {
                var stream = options.addStreamNode("gateway");
                if (!streamEndpoint.isBlank()) {
                    stream.bind(streamEndpoint);
                }
                if (!tlsStreamEndpoint.isBlank()) {
                    stream.bind(tlsStreamEndpoint)
                        .setTlsServer(
                            Env.get("ZLINK_JAVA_E2E_TLS_CERTIFICATE_PATH"),
                            Env.get("ZLINK_JAVA_E2E_TLS_KEY_PATH"));
                }
                stream.registerSession(ScenarioSession.class);
            }
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")));
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
}
