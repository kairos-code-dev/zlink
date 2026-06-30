package systems.zlink.e2e.yielddispatch.play;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.yielddispatch.shared.Contracts;
import systems.zlink.e2e.yielddispatch.shared.EnsureSpotHandler;
import systems.zlink.e2e.yielddispatch.shared.Env;
import systems.zlink.e2e.yielddispatch.shared.EvidenceHttpServer;
import systems.zlink.e2e.yielddispatch.shared.EvidenceStore;
import systems.zlink.e2e.yielddispatch.shared.PlayBindActorsHandler;
import systems.zlink.e2e.yielddispatch.shared.YieldActorFactory;
import systems.zlink.e2e.yielddispatch.shared.YieldEntrySpot;
import systems.zlink.e2e.yielddispatch.shared.YieldProbeHandlers;
import systems.zlink.e2e.yielddispatch.shared.YieldProbeSpot;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.ZLinkSpotManager;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.yielddispatch.play")
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
        return new EvidenceStore(nodeRid());
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
            String nodeRid = nodeRid();
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + nodeRid + "-flow.log")
                .traceLabel("java-yd-" + nodeRid);
            RouteMeshChannelBuilder route = options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            String routePeerEndpoint = Env.get("ZLINK_JAVA_E2E_ROUTE_PEER_ENDPOINT");
            if (!routePeerEndpoint.isBlank()) {
                route.enableClient(routePeerEndpoint);
            }
            route.addRequestHandler(
                PlayBindActorsHandler.class,
                Contracts.BindActorsRequest.class,
                Contracts.BindActorsReply.class);
            route.addRequestHandler(
                EnsureSpotHandler.Play.class,
                Contracts.EnsureSpotRequest.class,
                Contracts.EnsureSpotReply.class);
            options.addClientServerChannel(Contracts.DELAY_CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_DELAY_ENDPOINT"));
            options.useRegistrySpotRemoteAddresses(Contracts.SPOT_MESH)
                .setRouterChannelId(Contracts.ROUTE_CHANNEL);
            ZLinkSpotMeshBuilder spot = options.addSpotMesh(Contracts.SPOT_MESH);
            spot.enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            spot.addEntrySpot(YieldEntrySpot.class);
            spot.addSpotFactory(YieldProbeSpot.class);
            spot.addActorFactory(Contracts.ACTOR_TYPE, YieldActorFactory.class);
        };
    }

    @Bean
    ApplicationRunner createProbeSpot(ZLinkSpotManager spots) {
        return ignored -> spots.getOrCreate(
                YieldProbeSpot.class,
                RoutingId.from(Contracts.TARGET_SPOT),
                "bootstrap")
            .toCompletableFuture()
            .join();
    }

    @Bean
    YieldProbeHandlers.HoldHandler holdHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.HoldHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.YieldHandler yieldHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.YieldHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.WorkerYieldHandler workerYieldHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.WorkerYieldHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.ProbeHandler probeHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.ProbeHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.WorkerYieldCommandHandler workerYieldCommandHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.WorkerYieldCommandHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.ProbeCommandHandler probeCommandHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.ProbeCommandHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.YieldCommandHandler yieldCommandHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.YieldCommandHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.YieldTimeoutCommandHandler yieldTimeoutCommandHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.YieldTimeoutCommandHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.RemoteSpotYieldHandler remoteSpotYieldHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.RemoteSpotYieldHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.TimerStartCommandHandler timerStartCommandHandler() {
        return new YieldProbeHandlers.TimerStartCommandHandler();
    }

    @Bean
    YieldProbeHandlers.TimerStopCommandHandler timerStopCommandHandler() {
        return new YieldProbeHandlers.TimerStopCommandHandler();
    }

    @Bean
    YieldProbeHandlers.TimerTickHandler timerTickHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.TimerTickHandler(evidence);
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

    @Bean
    PlayBindActorsHandler playBindActorsHandler(
        systems.zlink.framework.actors.ZLinkActorManager actors,
        ZLinkSpotManager spots,
        EvidenceStore evidence) {
        return new PlayBindActorsHandler(actors, spots, evidence);
    }

    @Bean
    EnsureSpotHandler.Play playEnsureSpotHandler(
        ZLinkSpotManager spots,
        EvidenceStore evidence) {
        return new EnsureSpotHandler.Play(spots, evidence);
    }

    @Bean
    YieldProbeHandlers.SpotActorYieldHandler spotActorYieldHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.SpotActorYieldHandler(evidence);
    }

    @Bean
    YieldProbeHandlers.SpotActorFastHandler spotActorFastHandler(EvidenceStore evidence) {
        return new YieldProbeHandlers.SpotActorFastHandler(evidence);
    }

    private static String nodeRid() {
        return Env.get("ZLINK_JAVA_E2E_NODE_RID", Contracts.PLAY_NODE);
    }
}
