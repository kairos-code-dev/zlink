package systems.zlink.e2e.toactormessaging.session;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.toactormessaging.shared.Contracts;
import systems.zlink.e2e.toactormessaging.shared.Env;
import systems.zlink.e2e.toactormessaging.shared.EvidenceStore;
import systems.zlink.e2e.toactormessaging.shared.JsonHttp;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        Env.configure(args);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run();
        System.out.println("[boot] role=session rid="
            + Env.get("sessionRid") + " step=main run done");
    }

    @Bean
    EvidenceStore evidenceStore() {
        return new EvidenceStore();
    }

    @Bean(destroyMethod = "close")
    JsonHttp http(EvidenceStore evidence) {
        JsonHttp http = new JsonHttp(Env.get("sessionHttpEndpoint"));
        http.get("/health", () -> java.util.Map.of(
            "status", "ok", "rid", Env.get("sessionRid")));
        http.get("/evidence", evidence::all);
        http.start();
        return http;
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        return options -> {
            String rid = Env.get("sessionRid");
            options.addHandlersFromPackageOf(BindActorHandler.class);
            options.addLocationStore(new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
                .setConnectionString(Env.get("redisLocationEndpoint"))
                .setKeyPrefix(Env.get("locationKeyPrefix"))));
            options.addSpotMesh(Contracts.SPOT_MESH)
                .enableRouter(Env.get("sessionSpotEndpoint"))
                .setRoutingId(RoutingId.from(rid));
            options.addStreamNode("to-actor-" + rid)
                .bind(Env.get("sessionStreamEndpoint"))
                .registerSession(ToActorSession.class);
        };
    }

    public static final class ToActorSession implements ZLinkSession {
        private final ZLinkSessionContext context;
        private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;
        private final EvidenceStore evidence;

        public ToActorSession(
            ZLinkSessionContext context,
            ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers,
            EvidenceStore evidence) {
            this.context = context;
            this.handlers = handlers;
            this.evidence = evidence;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            evidence.append(new Contracts.ActorEvidence(
                "session", context.sessionId(), "session-connected", gatewayRid()));
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            CompletionStage<Void> notified = CompletableFuture.completedFuture(null);
            for (var actor : context.actors().bound()) {
                evidence.append(new Contracts.ActorEvidence(
                    "disconnect", actor.actorId(), "actor-disconnect-start", gatewayRid()));
                notified = notified.thenCompose(ignored -> actor.notifyDisconnected().thenRun(() ->
                    evidence.append(new Contracts.ActorEvidence(
                        "disconnect", actor.actorId(), "actor-disconnected", gatewayRid()))));
            }
            return notified.thenRun(() -> evidence.append(new Contracts.ActorEvidence(
                "session", context.sessionId(), "session-disconnected", gatewayRid())));
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            evidence.append(new Contracts.ActorEvidence(
                "session", context.sessionId(), "session-error", error.toString()));
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            systems.zlink.framework.messaging.ZLinkMessage payload) {
            return handlers.tryHandle(context, dispatch, payload).thenCompose(handled -> {
                if (handled) {
                    return CompletableFuture.completedFuture(null);
                }
                if (context.actors().bound().size() != 1) {
                    return CompletableFuture.failedFuture(new IllegalStateException(
                        "session relay requires exactly one bound actor"));
                }
                return context.actors().bound().get(0).relay(dispatch, payload);
            });
        }

        private static String gatewayRid() {
            return Env.get("sessionRid");
        }
    }

    public static final class BindActorHandler implements ZLinkTypedSessionPacketHandler<
        ZLinkSessionContext,
        Contracts.BindActorRequest> {
        private final EvidenceStore evidence;

        public BindActorHandler(EvidenceStore evidence) {
            this.evidence = evidence;
        }

        @Override
        public Class<Contracts.BindActorRequest> messageType() {
            return Contracts.BindActorRequest.class;
        }

        @Override
        public CompletionStage<Void> handle(
            ZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            Contracts.BindActorRequest request) {
            Contracts.ActorRefWire wire = request.actorRef();
            ActorRef actor = new ActorRef(
                RoutingId.fromHex(wire.nodeRidHex()), wire.actorId(), wire.generation());
            return context.actors().bindOrGet(actor).thenAccept(bound -> {
                evidence.append(new Contracts.ActorEvidence(
                    "bind", actor.actorId(), "actor-bound",
                    Env.get("sessionRid") + ":" + context.sessionId()));
                context.client().reply(new Contracts.BindActorReply(
                    actor.actorId(), actor.nodeRid().toString(), actor.generation())).submit();
            });
        }
    }
}
