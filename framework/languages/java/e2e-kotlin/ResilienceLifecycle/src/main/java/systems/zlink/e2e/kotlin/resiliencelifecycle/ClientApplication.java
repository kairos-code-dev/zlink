package systems.zlink.e2e.kotlin.resiliencelifecycle;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.registry.ZLinkRegistryQueryClient;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.kotlin.resiliencelifecycle.client")
public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) {
        ConfigurableApplicationContext context =
            new SpringApplicationBuilder(ClientApplication.class)
                .web(WebApplicationType.NONE)
                .run(args);
        try {
            context.getBean(ClientScenario.class)
                .run(Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "default"));
            System.out.println("resilience-lifecycle kotlin e2e result=passed");
        } finally {
            context.close();
        }
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    ZLinkFrameworkConfigurer clientFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/client-flow.log")
                .traceNodeId("kotlin-rl-client");
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"));
            options.addClientServerChannel(Contracts.CHANNEL).enableClient();
        };
    }

    @Bean
    ZLinkRegistryQueryClient registryQueryClient(ZLinkBackendAdapterFactory backendAdapterFactory) {
        return ZLinkRemoteRegistryQueryClient.connect(
            Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"),
            backendAdapterFactory);
    }

    @Bean
    ClientScenario clientScenario(
        systems.zlink.framework.channels.ZLinkClient client,
        ObjectProvider<ZLinkRegistryQueryClient> registry,
        ObjectMapper json) {
        return new ClientScenario(client, registry.getIfAvailable(), json);
    }
}
