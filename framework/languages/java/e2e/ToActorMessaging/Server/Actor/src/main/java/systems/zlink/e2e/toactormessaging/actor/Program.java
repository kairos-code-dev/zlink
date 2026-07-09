package systems.zlink.e2e.toactormessaging.actor;

import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.toactormessaging.shared.Contracts;
import systems.zlink.e2e.toactormessaging.shared.Env;
import systems.zlink.e2e.toactormessaging.shared.EvidenceStore;
import systems.zlink.e2e.toactormessaging.shared.JsonHttp;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
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
    JsonHttp http(EvidenceStore evidence, ZLinkActorManager actors) {
        boot("http create");
        JsonHttp http = new JsonHttp(Env.get("ZLINK_JAVA_E2E_ACTOR_HTTP"));
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
        http.post("/ensure-ref", Contracts.ActorCallRequest.class, request -> {
            var actor = actors.getOrCreate(request.actorId(), Contracts.ACTOR_TYPE, ZLinkMessage.of("create"))
                .toCompletableFuture()
                .join();
            return new Contracts.ActorRefWire(actor.nodeRid().toHex(), actor.actorId(), actor.generation());
        });
        boot("http start");
        http.start();
        boot("http start done");
        return http;
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        boot("framework configurer bean");
        return options -> {
            boot("configureDispatch");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs") + "/actor-flow.log")
                .traceLabel("java-to-actor-actor");
            boot("configureDispatch done");
            boot("addLocationStore");
            options.addLocationStore(new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
                .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
                .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX"))));
            boot("addLocationStore done");
            boot("addSpotMesh");
            var spotMesh = options.addSpotMesh(Contracts.SPOT_MESH);
            boot("addSpotMesh done");
            boot("enableRouter");
            spotMesh.enableRouter(Env.get("ZLINK_JAVA_E2E_ACTOR_SPOT"));
            boot("enableRouter done");
            boot("setRoutingId");
            spotMesh.setRoutingId(RoutingId.from(Env.get("ZLINK_JAVA_E2E_ACTOR_RID", "actor-a")));
            boot("setRoutingId done");
            boot("addEntrySpot");
            spotMesh.addEntrySpot(TestEntrySpot.class);
            boot("addEntrySpot done");
            boot("addActorFactory");
            spotMesh.addActorFactory(Contracts.ACTOR_TYPE, TestActorFactory.class);
            boot("addActorFactory done");
        };
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
        public ZLinkActor create(String actorId, ZLinkActorContext context) {
            return new TestActor(actorId, context);
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
        public void onCreateActor(TestActor actor, ZLinkMessage createRequest, CancellationToken cancellationToken) {
            evidence.append(new Contracts.ActorEvidence("create", actor.actorId(), "create", "created"));
        }

        @Override
        public ZLinkSpotActorJoinResponse onActorJoin(
            TestActor actor,
            ZLinkMessage request,
            CancellationToken cancellationToken) {
            evidence.append(new Contracts.ActorEvidence("join", actor.actorId(), "join", "joined"));
            return ZLinkSpotActorJoinResponse.accept();
        }
    }

    public static final class NotifyHandler
        implements ZLinkEntrySpotActorSendHandler<TestEntrySpot, TestActor, Contracts.ActorNotify> {
        private final EvidenceStore evidence;

        public NotifyHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public void handle(
            TestEntrySpot entrySpot,
            TestActor actor,
            ZLinkSpotActorSendContext context,
            Contracts.ActorNotify message,
            CancellationToken cancellationToken) {
            evidence.append(new Contracts.ActorEvidence(message.scenario(), actor.actorId(), "send", message.value()));
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
        public Contracts.ActorReply handle(
            TestEntrySpot entrySpot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            Contracts.ActorAsk request,
            CancellationToken cancellationToken) {
            evidence.append(new Contracts.ActorEvidence(request.scenario(), actor.actorId(), "request", request.value()));
            return new Contracts.ActorReply(request.scenario(), actor.actorId(), "reply:" + request.value());
        }
    }
}
