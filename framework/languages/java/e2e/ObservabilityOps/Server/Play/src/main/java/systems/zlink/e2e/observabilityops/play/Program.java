package systems.zlink.e2e.observabilityops.play;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.automaticturn.shared.Contracts;
import systems.zlink.e2e.automaticturn.shared.EnsureSpotHandler;
import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.e2e.automaticturn.shared.EvidenceHttpServer;
import systems.zlink.e2e.automaticturn.shared.EvidenceStore;
import systems.zlink.e2e.automaticturn.shared.PlayBindActorsHandler;
import systems.zlink.e2e.automaticturn.shared.AwaitActorFactory;
import systems.zlink.e2e.automaticturn.shared.AwaitEntrySpot;
import systems.zlink.e2e.automaticturn.shared.AwaitProbeHandlers;
import systems.zlink.e2e.automaticturn.shared.AwaitProbeSpot;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import io.micrometer.core.instrument.MeterRegistry;
import systems.zlink.e2e.automaticturn.shared.DrainEvidence;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.observabilityops.play")
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
    EvidenceHttpServer evidenceHttpServer(
        EvidenceStore evidence,
        ObjectMapper json,
        MeterRegistry metrics,
        systems.zlink.framework.monitoring.ZLinkDrainControl drain,
        ZLinkFrameworkLifecycle lifecycle,
        DrainEvidence drainEvidence,
        ZLinkSpotManager spots,
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new EvidenceHttpServer(
            evidence, json, Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"), metrics,
            drain, lifecycle::monitoringLocationRuntimeQuery, drainEvidence, spots::close,
            () -> routes.requestToNode(
                    Contracts.ROUTE_CHANNEL,
                    RoutingId.from(Contracts.PLAY_NODE_B),
                    new Contracts.EnsureSpotReq("obs-c5-source-route-ready"))
                .timeout(java.time.Duration.ofSeconds(30))
                .submit(Contracts.EnsureSpotRes.class)
                .thenApply(Contracts.EnsureSpotRes::nodeRid));
    }

    @Bean
    DrainEvidence drainEvidence() { return new DrainEvidence(); }

    @Bean(destroyMethod = "close")
    systems.zlink.e2e.automaticturn.shared.PersistentRoomEvents persistentRoomEvents() {
        return new systems.zlink.e2e.automaticturn.shared.PersistentRoomEvents(
            Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX"));
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            String nodeRid = nodeRid();
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + nodeRid + "-flow.log")
                .traceLabel("java-observability-" + nodeRid);
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            options.configureLocations().setSpotRouterChannel(
                Contracts.SPOT_MESH, Contracts.ROUTE_CHANNEL);
            String routePeerEndpoint = Env.get("ZLINK_JAVA_E2E_ROUTE_PEER_ENDPOINT");
            if (!routePeerEndpoint.isBlank()) {
                route.enableClient(routePeerEndpoint);
            }
            route.addRequestHandler(
                PlayBindActorsHandler.class,
                Contracts.BindActorsReq.class,
                Contracts.BindActorsRes.class);
            route.addRequestHandler(
                EnsureSpotHandler.Play.class,
                Contracts.EnsureSpotReq.class,
                Contracts.EnsureSpotRes.class);
            options.addClientServerChannel(Contracts.DELAY_CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_DELAY_ENDPOINT"));
            String fanoutEndpoint = Env.get("ZLINK_JAVA_E2E_OBS_FANOUT_ENDPOINT");
            if (!fanoutEndpoint.isBlank()) {
                var fanout = options.addFanoutChannel(Contracts.OBS_FANOUT_CHANNEL);
                if (Contracts.PLAY_NODE_A.equals(nodeRid)) {
                    fanout.enablePublisher(fanoutEndpoint);
                }
                fanout.enableSubscriber(fanoutEndpoint)
                    .addPublishHandler(
                        AwaitProbeHandlers.ObservabilityFanoutHandler.class,
                        Contracts.ObservabilityFanoutEvent.class);
            }
            ZLinkSpotMeshBuilder spot = options.addSpotMesh(Contracts.SPOT_MESH);
            String drainPolicy = Env.get("ZLINK_JAVA_E2E_SPOT_DRAIN_POLICY", "natural");
            spot.useDrainPolicy("release".equals(drainPolicy)
                ? systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy.RELEASE_AND_RECREATE
                : systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy.DRAIN_NATURAL);
            spot.enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            spot.addEntrySpot(AwaitEntrySpot.class);
            spot.addSpotFactory(AwaitProbeSpot.class);
            spot.addActorFactory(Contracts.ACTOR_TYPE, AwaitActorFactory.class);
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")));
    }

    @Bean
    ApplicationRunner createProbeSpot(ZLinkSpotManager spots) {
        return ignored -> {
            if (!Contracts.PLAY_NODE_A.equals(nodeRid())) {
                return;
            }
            spots.getOrCreate(
                    AwaitProbeSpot.class,
                    RoutingId.from(Contracts.TARGET_SPOT),
                    ZLinkMessage.of("bootstrap"))
                .whenComplete((created, failure) -> {
                    if (failure != null) {
                        System.getLogger(Program.class.getName()).log(
                            System.Logger.Level.ERROR,
                            "probe spot bootstrap failed",
                            failure);
                    }
                });
        };
    }

    @Bean
    AwaitProbeHandlers.HoldHandler holdHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.HoldHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.ObservabilityQueueHandler observabilityQueueHandler() {
        return new AwaitProbeHandlers.ObservabilityQueueHandler();
    }

    @Bean
    AwaitProbeHandlers.AwaitHandler awaitHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.AwaitHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.WorkerAwaitHandler workerAwaitHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.WorkerAwaitHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.ProbeHandler probeHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.ProbeHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.WorkerAwaitMsgHandler workerAwaitMsgHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.WorkerAwaitMsgHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.ProbeMsgHandler probeCommandHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.ProbeMsgHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.AwaitMsgHandler awaitCommandHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.AwaitMsgHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.AwaitTimeoutMsgHandler awaitTimeoutCommandHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.AwaitTimeoutMsgHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.AwaitCancelMsgHandler awaitCancelCommandHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.AwaitCancelMsgHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.RemoteSpotAwaitHandler remoteSpotAwaitHandler(
        EvidenceStore evidence,
        SpotHandleResolver spots) {
        return new AwaitProbeHandlers.RemoteSpotAwaitHandler(evidence, spots);
    }

    @Bean
    AwaitProbeHandlers.TimerStartMsgHandler timerStartCommandHandler() {
        return new AwaitProbeHandlers.TimerStartMsgHandler();
    }

    @Bean
    AwaitProbeHandlers.TimerStopMsgHandler timerStopCommandHandler() {
        return new AwaitProbeHandlers.TimerStopMsgHandler();
    }

    @Bean
    AwaitProbeHandlers.TimerTickHandler timerTickHandler(
        EvidenceStore evidence,
        ZLinkFanoutClient fanout) {
        return new AwaitProbeHandlers.TimerTickHandler(evidence, fanout);
    }

    @Bean
    AwaitProbeHandlers.ObservabilityFanoutHandler observabilityFanoutHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.ObservabilityFanoutHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.ActorAwaitHandler actorAwaitHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.ActorAwaitHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.ActorJoinHandler actorJoinHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.ActorJoinHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.ActorJoinAwaitHandler actorJoinAwaitHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.ActorJoinAwaitHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.ActorPushNotifyAwaitHandler actorPushAwaitHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.ActorPushNotifyAwaitHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.ActorFastHandler actorFastHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.ActorFastHandler(evidence);
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
    AwaitProbeHandlers.SpotActorAwaitHandler spotActorAwaitHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.SpotActorAwaitHandler(evidence);
    }

    @Bean
    AwaitProbeHandlers.SpotActorFastHandler spotActorFastHandler(EvidenceStore evidence) {
        return new AwaitProbeHandlers.SpotActorFastHandler(evidence);
    }

    private static String nodeRid() {
        return Env.get("ZLINK_JAVA_E2E_NODE_RID", Contracts.PLAY_NODE);
    }
}
