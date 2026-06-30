package systems.zlink.e2e.pubsub.subscriber;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.pubsub.shared.Contracts;
import systems.zlink.e2e.pubsub.subscriber.Configuration.SubscriberOptions;
import systems.zlink.e2e.pubsub.subscriber.Endpoints.OperationalEndpoints;
import systems.zlink.e2e.pubsub.subscriber.Handlers.EventNotifyHandler;
import systems.zlink.e2e.pubsub.subscriber.Handlers.EvidenceDispatchErrorObserver;
import systems.zlink.e2e.pubsub.subscriber.Infrastructure.EvidenceStore;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.pubsub.subscriber")
public final class SubscriberApplication {
    public AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(SubscriberApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    SubscriberOptions subscriberOptions() {
        return SubscriberOptions.fromEnv();
    }

    @Bean
    EvidenceStore evidenceStore(SubscriberOptions options) {
        return new EvidenceStore(options);
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    OperationalEndpoints operationalEndpoints(
        SubscriberOptions options,
        EvidenceStore evidence,
        ObjectMapper json) {
        return new OperationalEndpoints(options, evidence, json);
    }

    @Bean
    EvidenceDispatchErrorObserver evidenceDispatchErrorObserver(EvidenceStore evidence) {
        return new EvidenceDispatchErrorObserver(evidence);
    }

    @Bean
    ZLinkFrameworkConfigurer subscriberFramework(
        SubscriberOptions options,
        EvidenceStore evidence,
        EvidenceDispatchErrorObserver observer) {
        return framework -> {
            framework.codecs().addJson();
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/" + evidence.subscriberRid() + "-flow.log")
                .traceLabel("java-ps-" + evidence.subscriberRid())
                .setMessageFlowObserver(observer::observe);
            framework.addHandlersFromPackageOf(EventNotifyHandler.class);
            framework.useDiscovery().addRegistryEndpoint(options.registryRouterEndpoint());
            framework.addFanoutChannel(Contracts.EVENT_CHANNEL)
                .enableSubscriber()
                .addHandlerGroup(Contracts.HANDLER_GROUP);
        };
    }

    @Bean
    EventNotifyHandler eventNotifyHandler(EvidenceStore evidence) {
        return new EventNotifyHandler(evidence);
    }
}
