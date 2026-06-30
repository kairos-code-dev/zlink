package systems.zlink.e2e.discoveryregistryha.consumer;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
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
            framework.codecs().addJson();
            framework.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(options.logDir() + "/" + options.rid() + "-flow.log")
                .traceLabel(options.rid());
            for (String endpoint : options.discoveryEndpoints()) {
                framework.useDiscovery().addRegistryEndpoint(endpoint);
            }
            framework.addClientServerChannel(Contracts.CHANNEL).enableClient();
        };
    }

    @Bean
    ConsumerEndpoints consumerEndpoints(
        ConsumerOptions options,
        systems.zlink.framework.channels.ZLinkClient client,
        ObjectMapper json) {
        return new ConsumerEndpoints(options, client, json);
    }
}
