package systems.zlink.e2e.spotservice;

import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.ZLinkSpotManager;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotservice.play")
public final class PlayApplication {
    private PlayApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PlayApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
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
        ZLinkSpotManager spots) {
        return new EvidenceHttpServer(
            state,
            json,
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            spots);
    }

    @Bean
    ZLinkFrameworkConfigurer playFramework(ScenarioState state) {
        return options -> {
            String nodeRid = state.nodeRid();
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.addSpotRemoteAddressResolver(SpotRouteResolver.class);
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
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
            options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid))
                .addRequestHandler(
                    RoutePingHandler.class,
                    Contracts.RoutePing.class,
                    Contracts.RoutePong.class,
                    Contracts.ROUTE_PACKET);
            String peerIngress = "play-a".equals(nodeRid)
                ? Env.get("ZLINK_JAVA_E2E_INGRESS_B_ENDPOINT")
                : Env.get("ZLINK_JAVA_E2E_INGRESS_A_ENDPOINT");
            ClientServerChannelBuilder ingress = options.addClientServerChannel(Contracts.INGRESS_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_INGRESS_ENDPOINT"))
                .enableClient(peerIngress)
                .setRoutingId(RoutingId.from(nodeRid));
            ingress.addSendHandler(
                IngressCommandHandler.class,
                Contracts.OutboundCommand.class,
                "OutboundCommand");
            ingress.addRequestHandler(NoopIngressHandler.class, String.class, String.class, "Noop");
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH);
            node.enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                .enablePubSub(Env.get("ZLINK_JAVA_E2E_SPOT_PUB_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            node.addSpotFactory(UserSpot.class);
            node.addSpotFactory(MismatchedSpot.class);
        };
    }

    @Bean
    ApplicationRunner createOwnedSpot(
        ZLinkSpotManager spots,
        ScenarioState state) {
        return ignored -> {
            String spotRid = "play-a".equals(state.nodeRid()) ? "room-a" : "room-b";
            spots.getOrCreate(UserSpot.class, RoutingId.from(spotRid), "bootstrap")
                .toCompletableFuture()
                .join();
        };
    }
}
