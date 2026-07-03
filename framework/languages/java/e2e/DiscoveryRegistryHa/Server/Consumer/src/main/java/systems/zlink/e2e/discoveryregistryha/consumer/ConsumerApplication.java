package systems.zlink.e2e.discoveryregistryha.consumer;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class ConsumerApplication {
    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    ConsumerOptions consumerOptions() {
        return ConsumerOptions.fromEnv();
    }

    @Bean
    ZLinkFrameworkConfigurer consumerFramework(ConsumerOptions options) {
        return framework -> {
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/" + options.rid() + "-flow.log")
                .traceLabel(options.rid());
            framework.configureLocations().setHeartbeatInterval(Duration.ofMillis(options.heartbeatMillis()));
            framework.configureLocations().setOwnerLeaseTtl(Duration.ofMillis(options.leaseTtlMillis()));
            framework.configureLocations().setPollingInterval(Duration.ofMillis(options.pollingMillis()));
            framework.configureLocations().setStoreFailureGrace(Duration.ofMillis(options.storeFailureGraceMillis()));
            framework.addClientServerChannel(Contracts.CHANNEL).enableClient();
        };
    }

    @Bean
    ZLinkLocationStore locationStore(ConsumerOptions options) {
        ZLinkRedisLocationStore redisStore = new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(options.redisLocationEndpoint())
            .setKeyPrefix(options.locationKeyPrefix())
            .setCommandTimeout(Duration.ofMillis(options.redisCommandTimeoutMillis())));
        return "polling".equals(options.storeMode()) ? new PollingOnlyLocationStore(redisStore) : redisStore;
    }

    @Bean
    ConsumerEndpoints consumerEndpoints(
        ConsumerOptions options,
        systems.zlink.framework.channels.ZLinkClient client,
        systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle lifecycle,
        ObjectMapper json) {
        return new ConsumerEndpoints(options, client, lifecycle, json);
    }
}
