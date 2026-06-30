package systems.zlink.e2e.registrymessaging.consumer;

import java.util.Map;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.registrymessaging.consumer.Configuration.ConsumerOptions;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrymessaging.consumer")
public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.SERVLET)
            .properties(Map.of("server.port", ConsumerOptions.get("ZLINK_JAVA_E2E_HTTP_PORT", "0")))
            .run(args);
    }

    @Bean
    ZLinkFrameworkConfigurer consumerFramework() {
        return options -> {
            String logDir = ConsumerOptions.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + ConsumerOptions.get("ZLINK_JAVA_E2E_CONSUMER_NAME", "consumer") + "-flow.log")
                .traceLabel("java-rm-" + ConsumerOptions.get("ZLINK_JAVA_E2E_CONSUMER_NAME", "consumer"));

            String mode = ConsumerOptions.get("ZLINK_JAVA_E2E_CONSUMER_MODE", "discovery");
            var channel = options.addClientServerChannel(Contracts.API_CHANNEL);
            if ("discovery".equals(mode)) {
                options.useDiscovery().addRegistryEndpoint(ConsumerOptions.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
                channel.enableClient();
            } else {
                for (String endpoint : ConsumerOptions.get("ZLINK_JAVA_E2E_PROVIDER_ENDPOINTS").split(",")) {
                    if (!endpoint.isBlank()) {
                        channel.enableClient(endpoint);
                    }
                }
            }
        };
    }
}
