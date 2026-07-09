package systems.zlink.e2e.toactormessaging.caller;

import java.time.Duration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.toactormessaging.shared.Contracts;
import systems.zlink.e2e.toactormessaging.shared.Env;
import systems.zlink.e2e.toactormessaging.shared.JsonHttp;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

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

    @Bean(destroyMethod = "close")
    JsonHttp http(ZLinkActorClient actors, ZLinkActorDirectory actorRefs) {
        boot("http create");
        JsonHttp http = new JsonHttp(Env.get("ZLINK_JAVA_E2E_CALLER_HTTP"));
        boot("http route health");
        http.get("/health", () -> java.util.Map.of("status", "ok"));
        boot("http route send");
        http.post("/send", Contracts.ActorCallRequest.class, request -> {
            try {
                actors.sendToActor(
                        actorRef(actorRefs, request.actorId()),
                        new Contracts.ActorNotify(request.scenario(), request.actorId(), request.value()))
                    .packetName("ActorNotify")
                    .submit()
                    .toCompletableFuture()
                    .join();
                return Contracts.ActorCallResponse.ok(request.scenario(), request.actorId(), "sent");
            } catch (RuntimeException ex) {
                return failed(request, ex);
            }
        });
        boot("http route request");
        http.post("/request", Contracts.ActorCallRequest.class, request -> {
            try {
                Contracts.ActorReply reply = actors.requestToActor(
                        actorRef(actorRefs, request.actorId()),
                        new Contracts.ActorAsk(request.scenario(), request.actorId(), request.value()))
                    .packetName("ActorAsk")
                    .timeout(Duration.ofSeconds(5))
                    .submit(Contracts.ActorReply.class)
                    .toCompletableFuture()
                    .join();
                return Contracts.ActorCallResponse.ok(request.scenario(), request.actorId(), reply.value());
            } catch (RuntimeException ex) {
                return failed(request, ex);
            }
        });
        boot("http route send-ref");
        http.post("/send-ref", Contracts.ActorRefCallRequest.class, request -> {
            try {
                ActorRef actorRef = toActorRef(request.actorRef());
                actors.sendToActor(
                        actorRef,
                        new Contracts.ActorNotify(request.scenario(), actorRef.actorId(), request.value()))
                    .packetName("ActorNotify")
                    .submit()
                    .toCompletableFuture()
                    .join();
                return Contracts.ActorCallResponse.ok(request.scenario(), actorRef.actorId(), "sent");
            } catch (RuntimeException ex) {
                return failed(request, ex);
            }
        });
        boot("http route request-ref");
        http.post("/request-ref", Contracts.ActorRefCallRequest.class, request -> {
            try {
                ActorRef actorRef = toActorRef(request.actorRef());
                Contracts.ActorReply reply = actors.requestToActor(
                        actorRef,
                        new Contracts.ActorAsk(request.scenario(), actorRef.actorId(), request.value()))
                    .packetName("ActorAsk")
                    .timeout(Duration.ofSeconds(5))
                    .submit(Contracts.ActorReply.class)
                    .toCompletableFuture()
                    .join();
                return Contracts.ActorCallResponse.ok(request.scenario(), actorRef.actorId(), reply.value());
            } catch (RuntimeException ex) {
                return failed(request, ex);
            }
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
                .traceLogFile(Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs") + "/caller-flow.log")
                .traceLabel("java-to-actor-caller");
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
            spotMesh.enableRouter(Env.get("ZLINK_JAVA_E2E_CALLER_SPOT"));
            boot("enableRouter done");
            boot("setRoutingId");
            spotMesh.setRoutingId(RoutingId.from(Env.get("ZLINK_JAVA_E2E_CALLER_RID", "caller")));
            boot("setRoutingId done");
        };
    }

    private static void boot(String step) {
        System.out.println("[boot] role=caller step=" + step);
    }

    private static Contracts.ActorCallResponse failed(
        Contracts.ActorCallRequest request,
        RuntimeException ex) {
        return failed(request.scenario(), request.actorId(), ex);
    }

    private static Contracts.ActorCallResponse failed(
        Contracts.ActorRefCallRequest request,
        RuntimeException ex) {
        return failed(request.scenario(), request.actorRef().actorId(), ex);
    }

    private static Contracts.ActorCallResponse failed(
        String scenario,
        String actorId,
        RuntimeException ex) {
        Throwable current = ex;
        while (current.getCause() != null) {
            if (current instanceof ZLinkFrameworkException frameworkError) {
                return Contracts.ActorCallResponse.failed(
                    scenario,
                    actorId,
                    frameworkError.kind().name());
            }
            current = current.getCause();
        }
        if (current instanceof ZLinkFrameworkException frameworkError) {
            return Contracts.ActorCallResponse.failed(
                scenario,
                actorId,
                frameworkError.kind().name());
        }
        return Contracts.ActorCallResponse.failed(scenario, actorId, current.getClass().getSimpleName());
    }

    private static ActorRef actorRef(ZLinkActorDirectory actors, String actorId) {
        return actors.find(actorId)
            .thenApply(found -> found
                .orElseThrow(() -> new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ACTOR_ROUTE_NOT_FOUND,
                    "actor ref not found: " + actorId)))
            .toCompletableFuture()
            .join();
    }

    private static ActorRef toActorRef(Contracts.ActorRefWire ref) {
        return new ActorRef(RoutingId.fromHex(ref.nodeRidHex()), ref.actorId(), ref.generation());
    }
}
