package systems.zlink.e2e.pubsub;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.concurrent.CompletableFuture;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.pubsub.handlers.EventNotifyHandler;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.pubsub.handlers")
public final class SubscriberApplication {
    private SubscriberApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(SubscriberApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ScenarioState scenarioState() {
        return new ScenarioState(
            Env.get("ZLINK_JAVA_E2E_SUBSCRIBER_RID", "sub-1"),
            ScenarioState.topicsFromEnv());
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceHttpServer evidenceHttpServer(
        ScenarioState state,
        ObjectMapper json) {
        return new EvidenceHttpServer(state, json, Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkFrameworkConfigurer subscriberFramework(ScenarioState state) {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + state.subscriberRid() + "-flow.log")
                .traceNodeId("java-ps-" + state.subscriberRid())
                .setMessageDispatchErrorObserver(error -> {
                    state.record(
                        "DispatchError",
                        error.topic(),
                        "observer",
                        -1,
                        error.reason() + "/" + error.action() + "/" + error.packetName());
                    return CompletableFuture.completedFuture(null);
                });
            options.addHandlersFromPackageOf(EventNotifyHandler.class);
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
            options.addFanoutChannel(Contracts.EVENT_CHANNEL)
                .enableSubscriber()
                .addHandlerGroup(Contracts.HANDLER_GROUP);
        };
    }

    @Bean
    EventNotifyHandler eventNotifyHandler(ScenarioState state) {
        return new EventNotifyHandler(state);
    }
}
