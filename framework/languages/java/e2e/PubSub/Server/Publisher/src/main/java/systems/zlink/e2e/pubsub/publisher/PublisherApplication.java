package systems.zlink.e2e.pubsub.publisher;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.pubsub.publisher.Configuration.PublisherOptions;
import systems.zlink.e2e.pubsub.publisher.Endpoints.PublisherEndpoints;
import systems.zlink.e2e.pubsub.publisher.Infrastructure.EvidenceStore;
import systems.zlink.e2e.pubsub.shared.Contracts;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class PublisherApplication {
    public AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PublisherApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    PublisherOptions publisherOptions() {
        return PublisherOptions.fromEnv();
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceStore evidenceStore() {
        return new EvidenceStore();
    }

    @Bean
    PublisherEndpoints publisherEndpoints(
        PublisherOptions options,
        systems.zlink.framework.channels.ZLinkFanoutClient fanout,
        EvidenceStore evidence,
        ObjectMapper json) {
        return new PublisherEndpoints(options, fanout, evidence, json);
    }

    @Bean
    ZLinkFrameworkConfigurer publisherFramework(PublisherOptions options) {
        return framework -> {
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/publisher-flow.log")
                .traceLabel("java-ps-publisher");
            framework.addFanoutChannel(Contracts.EVENT_CHANNEL)
                .enablePublisher(options.publisherEndpoint());
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore(PublisherOptions options) {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(options.redisLocationEndpoint())
            .setKeyPrefix(options.locationKeyPrefix()));
    }
}
