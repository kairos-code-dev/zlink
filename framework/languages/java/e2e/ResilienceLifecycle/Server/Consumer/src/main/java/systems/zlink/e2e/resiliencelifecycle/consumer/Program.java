package systems.zlink.e2e.resiliencelifecycle.consumer;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.context.annotation.Bean;
import systems.zlink.e2e.resiliencelifecycle.consumer.endpoints.ConsumerEndpoints;
import systems.zlink.e2e.resiliencelifecycle.consumer.scenarios.ConsumerScenario;
import systems.zlink.e2e.resiliencelifecycle.shared.Contracts;
import systems.zlink.e2e.resiliencelifecycle.shared.Env;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.resiliencelifecycle.consumer")
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
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    ConsumerEndpoints consumerEndpoints(ObjectMapper json, ConsumerScenario scenario) {
        return new ConsumerEndpoints(json, scenario, Env.get("ZLINK_JAVA_E2E_CONSUMER_HTTP_ENDPOINT"));
    }

    @Bean
    ZLinkFrameworkConfigurer consumerFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/consumer-flow.log")
                .traceLabel("java-rl-consumer");
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
            options.addClientServerChannel(Contracts.CHANNEL).enableClient();
        };
    }

    @Bean
    ZLinkRegistryQueryClient registryQueryClient(ZLinkBackendAdapterFactory backendAdapterFactory) {
        return ZLinkRemoteRegistryQueryClient.connect(
            Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"),
            backendAdapterFactory);
    }

    @Bean
    ConsumerScenario consumerScenario(
        systems.zlink.framework.channels.ZLinkClient client,
        ObjectProvider<ZLinkRegistryQueryClient> registry,
        ObjectMapper json) {
        return new ConsumerScenario(client, registry.getIfAvailable(), json);
    }
}
