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
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
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
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run(args);
    }

    @Bean(destroyMethod = "close")
    JsonHttp http(ZLinkActorClient actors) {
        JsonHttp http = new JsonHttp(Env.get("ZLINK_JAVA_E2E_CALLER_HTTP"));
        http.get("/health", () -> java.util.Map.of("status", "ok"));
        http.post("/send", Contracts.ActorCallRequest.class, request -> {
            try {
                actors.sendToActor(
                        request.actorId(),
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
        http.post("/request", Contracts.ActorCallRequest.class, request -> {
            try {
                Contracts.ActorReply reply = actors.requestToActor(
                        request.actorId(),
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
        http.start();
        return http;
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        return options -> {
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs") + "/caller-flow.log")
                .traceLabel("java-to-actor-caller");
            options.addLocationStore(new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
                .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
                .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX"))));
            options.addSpotMesh(Contracts.SPOT_MESH)
                .enableRouter(Env.get("ZLINK_JAVA_E2E_CALLER_SPOT"))
                .setRoutingId(RoutingId.from(Env.get("ZLINK_JAVA_E2E_CALLER_RID", "caller")));
        };
    }

    private static Contracts.ActorCallResponse failed(
        Contracts.ActorCallRequest request,
        RuntimeException ex) {
        Throwable current = ex;
        while (current.getCause() != null) {
            if (current instanceof ZLinkFrameworkException frameworkError) {
                return Contracts.ActorCallResponse.failed(
                    request.scenario(),
                    request.actorId(),
                    frameworkError.kind().name());
            }
            current = current.getCause();
        }
        if (current instanceof ZLinkFrameworkException frameworkError) {
            return Contracts.ActorCallResponse.failed(
                request.scenario(),
                request.actorId(),
                frameworkError.kind().name());
        }
        return Contracts.ActorCallResponse.failed(request.scenario(), request.actorId(), current.getClass().getSimpleName());
    }
}
