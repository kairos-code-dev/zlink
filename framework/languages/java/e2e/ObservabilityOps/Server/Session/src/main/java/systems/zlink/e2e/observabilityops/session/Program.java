package systems.zlink.e2e.observabilityops.session;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.automaticturn.shared.BindActorsHandler;
import systems.zlink.e2e.automaticturn.shared.Contracts;
import systems.zlink.e2e.automaticturn.shared.EnsureSpotHandler;
import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.e2e.automaticturn.shared.EvidenceHttpServer;
import systems.zlink.e2e.automaticturn.shared.EvidenceStore;
import systems.zlink.e2e.automaticturn.shared.ScenarioReqHandler;
import systems.zlink.e2e.automaticturn.shared.ShutdownAwaitSessionHandlers;
import systems.zlink.e2e.automaticturn.shared.SpotCommandHandler;
import systems.zlink.e2e.automaticturn.shared.RemoteSpotAwaitSessionHandler;
import systems.zlink.e2e.automaticturn.shared.AwaitActorFactory;
import systems.zlink.e2e.automaticturn.shared.AwaitEntrySpot;
import systems.zlink.e2e.automaticturn.shared.AwaitProbeHandlers;
import systems.zlink.e2e.automaticturn.shared.AwaitProbeSpot;
import systems.zlink.e2e.automaticturn.shared.AwaitSession;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.SpotHandleResolver;
import io.micrometer.core.instrument.MeterRegistry;
import systems.zlink.e2e.automaticturn.shared.DrainEvidence;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;
import org.springframework.boot.ApplicationRunner;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.messaging.ZLinkMessage;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.observabilityops.session")
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
    EvidenceHttpServer evidenceHttpServer(
        EvidenceStore evidence,
        ObjectMapper json,
        MeterRegistry metrics,
        systems.zlink.framework.monitoring.ZLinkDrainControl drain,
        ZLinkFrameworkLifecycle lifecycle,
        DrainEvidence drainEvidence) {
        return new EvidenceHttpServer(
            evidence, json, Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"), metrics,
            drain, lifecycle::monitoringLocationRuntimeQuery, drainEvidence, null, null);
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
            options.addHandlersFromPackageOf(ScenarioReqHandler.class);
            var dispatch = options.configureDispatch()
                .messageFlow("off".equals(Env.get("ZLINK_JAVA_E2E_MESSAGE_FLOW"))
                    ? ZLinkMessageFlowLogMode.OFF
                    : ZLinkMessageFlowLogMode.KEY_TRANSITIONS);
            if (!"off".equals(Env.get("ZLINK_JAVA_E2E_MESSAGE_FLOW"))) {
                dispatch.traceLogFile(logDir + "/session-flow.log")
                    .traceLabel("java-observability-session");
            }
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_SESSION_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from("session-a"));
            String routeBEndpoint = Env.get("ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT");
            if (!routeBEndpoint.isBlank()) {
                route.enableClient(routeBEndpoint);
            }
            options.addClientServerChannel(Contracts.DELAY_CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_DELAY_ENDPOINT"));
            ZLinkSpotMeshBuilder spot = options.addSpotMesh(Contracts.SPOT_MESH);
            spot.enableRouter(Env.get("ZLINK_JAVA_E2E_SESSION_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from("session-a"));
            spot.addEntrySpot(AwaitEntrySpot.class);
            spot.addSpotFactory(AwaitProbeSpot.class);
            spot.addActorFactory(Contracts.ACTOR_TYPE, AwaitActorFactory.class);
            options.addStreamNode("session")
                .bind(Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"))
                .registerSession(AwaitSession.class);
        };
    }

    @Bean
    ApplicationRunner createDrainSpot(ZLinkSpotManager spots) {
        return ignored -> {
            String spotRid = Env.get("ZLINK_JAVA_E2E_SESSION_DRAIN_SPOT");
            if (!spotRid.isBlank()) {
                spots.getOrCreate(
                    AwaitProbeSpot.class,
                    RoutingId.from(spotRid),
                    ZLinkMessage.of("drain-hold")).toCompletableFuture().join();
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
    ScenarioReqHandler scenarioRequestHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots,
        EvidenceStore evidence) {
        return new ScenarioReqHandler(routes, spots, evidence);
    }

    @Bean
    ShutdownAwaitSessionHandlers.Wait shutdownAwaitWaitHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new ShutdownAwaitSessionHandlers.Wait(routes, spots);
    }

    @Bean
    ShutdownAwaitSessionHandlers.Recovery shutdownAwaitRecoveryHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new ShutdownAwaitSessionHandlers.Recovery(routes, spots);
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
    systems.zlink.e2e.automaticturn.shared.PersistentRoomStateSessionHandler persistentRoomStateSessionHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        systems.zlink.framework.spots.SpotHandleResolver spots) {
        return new systems.zlink.e2e.automaticturn.shared.PersistentRoomStateSessionHandler(
            routes, spots);
    }

    @Bean
    RemoteSpotAwaitSessionHandler remoteSpotAwaitSessionHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new RemoteSpotAwaitSessionHandler(routes, spots);
    }

    @Bean
    SpotCommandHandler.WorkerAwait workerAwaitMsgHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new SpotCommandHandler.WorkerAwait(routes, spots);
    }

    @Bean
    SpotCommandHandler.Await awaitCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new SpotCommandHandler.Await(routes, spots);
    }

    @Bean
    SpotCommandHandler.AwaitTimeout awaitTimeoutCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new SpotCommandHandler.AwaitTimeout(routes, spots);
    }

    @Bean
    SpotCommandHandler.AwaitCancel awaitCancelCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new SpotCommandHandler.AwaitCancel(routes, spots);
    }

    @Bean
    SpotCommandHandler.Probe probeCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new SpotCommandHandler.Probe(routes, spots);
    }

    @Bean
    SpotCommandHandler.ProbeRequest probeRequestHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new SpotCommandHandler.ProbeRequest(routes, spots);
    }

    @Bean
    SpotCommandHandler.TimerStart timerStartCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new SpotCommandHandler.TimerStart(routes, spots);
    }

    @Bean
    SpotCommandHandler.TimerStop timerStopCommandHandler(
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        SpotHandleResolver spots) {
        return new SpotCommandHandler.TimerStop(routes, spots);
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
}
