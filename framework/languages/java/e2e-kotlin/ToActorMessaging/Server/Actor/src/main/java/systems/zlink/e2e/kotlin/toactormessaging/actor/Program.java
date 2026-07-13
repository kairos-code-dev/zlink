package systems.zlink.e2e.kotlin.toactormessaging.actor;

import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import java.time.Instant;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.kotlin.toactormessaging.shared.Contracts;
import systems.zlink.e2e.kotlin.toactormessaging.shared.Env;
import systems.zlink.e2e.kotlin.toactormessaging.shared.EvidenceStore;
import systems.zlink.e2e.kotlin.toactormessaging.shared.JsonHttp;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class Program {
    private final ConcurrentHashMap<String, ZLinkActorLocation> liveActorRows = new ConcurrentHashMap<>();

    private Program() {
    }

    public static void main(String... args) {
        boot("main builder");
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        boot("main keepAlive");
        builder.application().setKeepAlive(true);
        boot("main run");
        builder.run(args);
        boot("main run done");
    }

    @Bean
    EvidenceStore evidenceStore() {
        boot("evidenceStore");
        return new EvidenceStore();
    }

    @Bean(destroyMethod = "close")
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_KOTLIN_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_KOTLIN_E2E_LOCATION_KEY_PREFIX")));
    }

    @Bean(destroyMethod = "close")
    JsonHttp http(EvidenceStore evidence, ZLinkActorManager actors, ZLinkRedisLocationStore locations) {
        boot("http create");
        JsonHttp http = new JsonHttp(Env.get("ZLINK_KOTLIN_E2E_ACTOR_HTTP"));
        boot("http route health");
        http.get("/health", () -> java.util.Map.of("status", "ok"));
        boot("http route evidence");
        http.get("/evidence", evidence::all);
        boot("http route ensure");
        http.post("/ensure", Contracts.ActorCallRequest.class, request -> {
            actors.getOrCreate(request.actorId(), Contracts.ACTOR_TYPE, ZLinkMessage.of("create"))
                .toCompletableFuture()
                .join();
            return Contracts.ActorCallResponse.ok(request.scenario(), request.actorId(), "ensured");
        });
        boot("http route fault stale");
        http.post("/fault/stale", Contracts.ActorFaultRequest.class, request -> {
            ZLinkActorLocation live = ensureLiveRow(actors, locations, request.actorId());
            liveActorRows.put(request.actorId(), live);
            writeActorRow(locations, staleRow(live));
            return Contracts.ActorCallResponse.ok(request.scenario(), request.actorId(), "stale");
        });
        boot("http route fault route");
        http.post("/fault/route-disconnected", Contracts.ActorFaultRequest.class, request -> {
            ZLinkActorLocation live = ensureLiveRow(actors, locations, request.actorId());
            liveActorRows.put(request.actorId(), live);
            writeActorRow(locations, disconnectedRouteRow(live));
            return Contracts.ActorCallResponse.ok(request.scenario(), request.actorId(), "route-disconnected");
        });
        boot("http route fault restore");
        http.post("/fault/restore", Contracts.ActorFaultRequest.class, request -> {
            ZLinkActorLocation current = resolveActorRow(locations, request.actorId());
            ZLinkActorLocation live = liveActorRows.get(request.actorId());
            if (live == null) {
                live = ensureLiveRow(actors, locations, request.actorId());
            }
            writeActorRow(locations, restoreRow(live, current));
            return Contracts.ActorCallResponse.ok(request.scenario(), request.actorId(), "restored");
        });
        boot("http start");
        http.start();
        boot("http start done");
        return http;
    }

    @Bean
    ZLinkFrameworkConfigurer framework(ZLinkRedisLocationStore locationStore) {
        boot("framework configurer bean");
        return options -> {
            boot("configureDispatch");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs") + "/actor-flow.log")
                .traceLabel("kotlin-to-actor-actor");
            boot("configureDispatch done");
            boot("addLocationStore");
            options.addLocationStore(locationStore);
            boot("addLocationStore done");
            boot("addSpotMesh");
            var spotMesh = options.addSpotMesh(Contracts.SPOT_MESH);
            boot("addSpotMesh done");
            boot("enableRouter");
            spotMesh.enableRouter(Env.get("ZLINK_KOTLIN_E2E_ACTOR_SPOT"));
            boot("enableRouter done");
            boot("setRoutingId");
            spotMesh.setRoutingId(RoutingId.from(Env.get("ZLINK_KOTLIN_E2E_ACTOR_RID", "actor-a")));
            boot("setRoutingId done");
            boot("addEntrySpot");
            spotMesh.addEntrySpot(TestEntrySpot.class);
            boot("addEntrySpot done");
            boot("addActorFactory");
            spotMesh.addActorFactory(Contracts.ACTOR_TYPE, TestActorFactory.class);
            boot("addActorFactory done");
        };
    }

    private static ZLinkActorLocation ensureLiveRow(
        ZLinkActorManager actors,
        ZLinkRedisLocationStore locations,
        String actorId) {
        actors.getOrCreate(actorId, Contracts.ACTOR_TYPE, ZLinkMessage.of("create"))
            .toCompletableFuture()
            .join();
        return waitForActorRow(locations, actorId);
    }

    private static ZLinkActorLocation waitForActorRow(ZLinkRedisLocationStore locations, String actorId) {
        long deadline = System.nanoTime() + 5_000_000_000L;
        ZLinkActorLocation row = null;
        while (System.nanoTime() < deadline) {
            row = resolveActorRow(locations, actorId);
            if (row != null) {
                return row;
            }
            sleepBriefly();
        }
        throw new IllegalStateException("actor location row was not published actorId=" + actorId);
    }

    private static ZLinkActorLocation resolveActorRow(ZLinkRedisLocationStore locations, String actorId) {
        return locations.resolveActor(new ZLinkActorLocationKey(actorId))
            .toCompletableFuture()
            .join();
    }

    private static ZLinkActorLocation staleRow(ZLinkActorLocation live) {
        return copy(
            live,
            live.nodeRid(),
            new ActorRef(live.actorRef().nodeRid(), live.actorRef().actorId(), live.actorRef().generation() + 1000),
            live.generation() + 1);
    }

    private static ZLinkActorLocation disconnectedRouteRow(ZLinkActorLocation live) {
        RoutingId disconnectedRid = RoutingId.from("missing-route-" + live.actorId());
        return copy(
            live,
            disconnectedRid,
            new ActorRef(disconnectedRid, live.actorRef().actorId(), live.actorRef().generation()),
            live.generation() + 1);
    }

    private static ZLinkActorLocation restoreRow(ZLinkActorLocation live, ZLinkActorLocation current) {
        long generation = Math.max(live.generation(), current == null ? 0 : current.generation()) + 1;
        return copy(live, live.nodeRid(), live.actorRef(), generation);
    }

    private static ZLinkActorLocation copy(
        ZLinkActorLocation source,
        RoutingId nodeRid,
        ActorRef actorRef,
        long generation) {
        return ZLinkActorLocation.fromActorRef(
            source.actorId(),
            source.actorType(),
            actorRef,
            nodeRid,
            source.locationKind(),
            source.spotMeshName(),
            source.spotRid(),
            source.ownerId(),
            generation,
            Instant.now());
    }

    private static void writeActorRow(ZLinkRedisLocationStore locations, ZLinkActorLocation row) {
        var result = locations.updateActor(row, ZLinkLocationWriteIntent.TAKEOVER)
            .toCompletableFuture()
            .join();
        if (result.status() != ZLinkLocationWriteStatus.STORED) {
            throw new IllegalStateException("actor location write failed actorId="
                + row.actorId()
                + " status="
                + result.status());
        }
    }

    private static void sleepBriefly() {
        try {
            Thread.sleep(100);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("actor location wait interrupted", ex);
        }
    }

    @Bean
    ApplicationRunner createBaselineActors(ZLinkActorManager actors) {
        return ignored -> {
            boot("baselineActors start");
            for (String actorId : java.util.List.of("ta-a1", "ta-a2", "ta-a3", "ta-a4", "ta-b2", "ta-b3")) {
                boot("baselineActors getOrCreate actorId=" + actorId);
                actors.getOrCreate(actorId, Contracts.ACTOR_TYPE, ZLinkMessage.of("create"))
                    .toCompletableFuture()
                    .join();
                boot("baselineActors getOrCreate done actorId=" + actorId);
            }
            boot("baselineActors done");
        };
    }

    private static void boot(String step) {
        System.out.println("[boot] role=actor step=" + step);
    }

    public static final class TestActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;

        TestActor(String actorId, ZLinkActorContext context) {
            this.actorId = actorId;
            this.context = context;
        }

        @Override public String actorId() { return actorId; }
        @Override public ZLinkActorContext context() { return context; }
    }

    public static final class TestActorFactory implements ZLinkActorFactory {
        @Override
        public CompletionStage<ZLinkActor> create(String actorId, ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new TestActor(actorId, context));
        }
    }

    public static final class TestEntrySpot implements ZLinkEntrySpot<TestActor> {
        private final ZLinkEntrySpotContext context;
        private final EvidenceStore evidence;

        public TestEntrySpot(ZLinkEntrySpotContext context, EvidenceStore evidence) {
            this.context = context;
            this.evidence = evidence;
        }

        @Override public ZLinkEntrySpotContext context() { return context; }

        @Override
        public void configure() {
            context.handlers().addHandler(NotifyHandler.class);
            context.handlers().addHandler(AskHandler.class);
        }

        @Override
        public CompletionStage<Void> onCreateActor(TestActor actor, ZLinkMessage createRequest) {
            evidence.append(new Contracts.ActorEvidence("create", actor.actorId(), "create", "created"));
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
            String actorId,
            ZLinkMessage request) {
            evidence.append(new Contracts.ActorEvidence("admission", actorId, "join", "accepted"));
            return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept());
        }

        @Override
        public CompletionStage<Void> onJoinedActor(TestActor actor) {
            evidence.append(new Contracts.ActorEvidence("join", actor.actorId(), "join", "joined"));
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(TestActor actor) {
            evidence.append(new Contracts.ActorEvidence("leave", actor.actorId(), "join", "left"));
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class NotifyHandler
        implements ZLinkEntrySpotActorSendHandler<TestEntrySpot, TestActor, Contracts.ActorNotify> {
        private final EvidenceStore evidence;

        public NotifyHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Void> handle(
            TestEntrySpot entrySpot,
            TestActor actor,
            ZLinkSpotActorSendContext context,
            Contracts.ActorNotify message) {
            evidence.append(new Contracts.ActorEvidence(message.scenario(), actor.actorId(), "send", message.value()));
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class AskHandler
        implements ZLinkEntrySpotActorRequestHandler<
            TestEntrySpot,
            TestActor,
            Contracts.ActorAsk,
            Contracts.ActorReply> {
        private final EvidenceStore evidence;

        public AskHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public CompletionStage<Contracts.ActorReply> handle(
            TestEntrySpot entrySpot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorAsk request) {
            evidence.append(new Contracts.ActorEvidence(request.scenario(), actor.actorId(), "request", request.value()));
            return CompletableFuture.completedFuture(new Contracts.ActorReply(
                request.scenario(), actor.actorId(), "reply:" + request.value()));
        }
    }
}
