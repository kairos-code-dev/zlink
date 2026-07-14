package systems.zlink.e2e.resiliencelifecycle.provider;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.resiliencelifecycle.provider.endpoints.EvidenceHttpServer;
import systems.zlink.e2e.resiliencelifecycle.provider.handlers.RuntimeErrorEvidenceHandler;
import systems.zlink.e2e.resiliencelifecycle.provider.handlers.WorkMsgHandler;
import systems.zlink.e2e.resiliencelifecycle.provider.handlers.WorkReqHandler;
import systems.zlink.e2e.resiliencelifecycle.provider.infrastructure.ScenarioState;
import systems.zlink.e2e.resiliencelifecycle.shared.Contracts;
import systems.zlink.e2e.resiliencelifecycle.shared.Env;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.resiliencelifecycle.provider")
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
    ScenarioState scenarioState() {
        return new ScenarioState(Env.get("ZLINK_JAVA_E2E_PROVIDER_RID", "provider-a"));
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceHttpServer evidenceHttpServer(
        ScenarioState state,
        ObjectMapper json,
        ZLinkChannelRuntimeOptions runtimeOptions,
        ConfigurableApplicationContext applicationContext) {
        return new EvidenceHttpServer(
            state,
            json,
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            runtimeOptions,
            applicationContext);
    }

    @Bean
    ZLinkFrameworkConfigurer providerFramework(ScenarioState state) {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureLocations().setHeartbeatInterval(Duration.ofMillis(500));
            options.configureLocations().setOwnerLeaseTtl(Duration.ofSeconds(2));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + state.providerRid() + "-flow.log")
                .traceLabel("java-rl-" + state.providerRid())
                .setMessageFlowObserver(error -> {
                    if (error.outcome() != ZLinkMessageFlowOutcome.ERROR) {
                        return CompletableFuture.completedFuture(null);
                    }
                    state.record(
                        "DispatchError",
                        error.errorReason() + "/" + error.errorAction() + "/" + error.packetName());
                    if (state.observerThrows()) {
                        throw new IllegalStateException("dispatch observer failure");
                    }
                    return CompletableFuture.completedFuture(null);
                });
            options.addHandlersFromPackageOf(WorkReqHandler.class);
            options.addClientServerChannel(Contracts.CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_API_ENDPOINT"))
                .setRoutingId(RoutingId.from(state.providerRid()))
                .addHandlerGroup(Contracts.HANDLER_GROUP);
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")));
    }

    @Bean
    WorkReqHandler workRequestHandler(ScenarioState state) {
        return new WorkReqHandler(state);
    }

    @Bean
    WorkMsgHandler workCommandHandler(ScenarioState state) {
        return new WorkMsgHandler(state);
    }

    @Bean
    RuntimeErrorEvidenceHandler runtimeErrorEvidenceHandler(ScenarioState state) {
        return new RuntimeErrorEvidenceHandler(state);
    }
}
