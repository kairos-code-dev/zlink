package systems.zlink.e2e.yielddispatch.session;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.yielddispatch.shared.BindActorsHandler;
import systems.zlink.e2e.yielddispatch.shared.Contracts;
import systems.zlink.e2e.yielddispatch.shared.EnsureSpotHandler;
import systems.zlink.e2e.yielddispatch.shared.Env;
import systems.zlink.e2e.yielddispatch.shared.EvidenceHttpServer;
import systems.zlink.e2e.yielddispatch.shared.EvidenceStore;
import systems.zlink.e2e.yielddispatch.shared.ScenarioRequestHandler;
import systems.zlink.e2e.yielddispatch.shared.ShutdownYieldSessionHandlers;
import systems.zlink.e2e.yielddispatch.shared.SpotCommandHandler;
import systems.zlink.e2e.yielddispatch.shared.RemoteSpotYieldSessionHandler;
import systems.zlink.e2e.yielddispatch.shared.YieldActorFactory;
import systems.zlink.e2e.yielddispatch.shared.YieldEntrySpot;
import systems.zlink.e2e.yielddispatch.shared.YieldProbeHandlers;
import systems.zlink.e2e.yielddispatch.shared.YieldSession;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.yielddispatch.session")
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
    EvidenceStore evidenceStore() {
        return new EvidenceStore("session-a");
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceHttpServer evidenceHttpServer(EvidenceStore evidence, ObjectMapper json) {
        return new EvidenceHttpServer(evidence, json, Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/session-flow.log")
                .traceLabel("java-yd-session");
            RouteMeshChannelBuilder route = options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_SESSION_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from("session-a"));
            String routeBEndpoint = Env.get("ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT");
            if (!routeBEndpoint.isBlank()) {
                route.enableClient(routeBEndpoint);
            }
            options.addClientServerChannel(Contracts.DELAY_CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_DELAY_ENDPOINT"));
            options.useRegistrySpotRemoteAddresses(Contracts.SPOT_MESH)
                .setRouterChannelId(Contracts.ROUTE_CHANNEL);
            ZLinkSpotMeshBuilder spot = options.addSpotMesh(Contracts.SPOT_MESH);
            spot.enableRouter(Env.get("ZLINK_JAVA_E2E_SESSION_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from("session-a"));
            spot.addEntrySpot(YieldEntrySpot.class);
            spot.addActorFactory(Contracts.ACTOR_TYPE, YieldActorFactory.class);
            options.addStreamNode("session")
                .bind(Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"))
                .registerSession(YieldSession.class)
                .addSessionPacketHandler(ScenarioRequestHandler.class)
                .addSessionPacketHandler(ShutdownYieldSessionHandlers.Wait.class)
                .addSessionPacketHandler(ShutdownYieldSessionHandlers.Recovery.class)
                .addSessionPacketHandler(SpotCommandHandler.Yield.class)
                .addSessionPacketHandler(SpotCommandHandler.YieldTimeout.class)
                .addSessionPacketHandler(SpotCommandHandler.WorkerYield.class)
                .addSessionPacketHandler(SpotCommandHandler.Probe.class)
                .addSessionPacketHandler(SpotCommandHandler.TimerStart.class)
                .addSessionPacketHandler(SpotCommandHandler.TimerStop.class)
                .addSessionPacketHandler(EnsureSpotHandler.class)
                .addSessionPacketHandler(RemoteSpotYieldSessionHandler.class)
                .addSessionPacketHandler(BindActorsHandler.class);
        };
    }

    @Bean
    ScenarioRequestHandler scenarioRequestHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        EvidenceStore evidence) {
        return new ScenarioRequestHandler(routes, evidence);
    }

    @Bean
    ShutdownYieldSessionHandlers.Wait shutdownYieldWaitHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new ShutdownYieldSessionHandlers.Wait(routes);
    }

    @Bean
    ShutdownYieldSessionHandlers.Recovery shutdownYieldRecoveryHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new ShutdownYieldSessionHandlers.Recovery(routes);
    }

    @Bean
    BindActorsHandler bindActorsHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        EvidenceStore evidence) {
        return new BindActorsHandler(routes, evidence);
    }

    @Bean
    EnsureSpotHandler ensureSpotHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new EnsureSpotHandler(routes);
    }

    @Bean
    RemoteSpotYieldSessionHandler remoteSpotYieldSessionHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new RemoteSpotYieldSessionHandler(routes);
    }

    @Bean
    SpotCommandHandler.WorkerYield workerYieldCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new SpotCommandHandler.WorkerYield(routes);
    }

    @Bean
    SpotCommandHandler.Yield yieldCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new SpotCommandHandler.Yield(routes);
    }

    @Bean
    SpotCommandHandler.YieldTimeout yieldTimeoutCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new SpotCommandHandler.YieldTimeout(routes);
    }

    @Bean
    SpotCommandHandler.Probe probeCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new SpotCommandHandler.Probe(routes);
    }

    @Bean
    SpotCommandHandler.TimerStart timerStartCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new SpotCommandHandler.TimerStart(routes);
    }

    @Bean
    SpotCommandHandler.TimerStop timerStopCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new SpotCommandHandler.TimerStop(routes);
    }

    @Bean
    YieldProbeHandlers.ActorYieldHandler actorYieldHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.ActorYieldHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.ActorJoinHandler actorJoinHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.ActorJoinHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.ActorJoinYieldHandler actorJoinYieldHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.ActorJoinYieldHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.ActorPushYieldHandler actorPushYieldHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.ActorPushYieldHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.ActorFastHandler actorFastHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.ActorFastHandler(evidence);
    }
}
