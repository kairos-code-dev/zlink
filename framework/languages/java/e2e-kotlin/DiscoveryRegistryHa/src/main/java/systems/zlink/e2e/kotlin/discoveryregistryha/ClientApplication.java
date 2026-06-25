package systems.zlink.e2e.kotlin.discoveryregistryha;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
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
    scanBasePackages = "systems.zlink.e2e.kotlin.discoveryregistryha.client")
public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) {
        ConfigurableApplicationContext context =
            new SpringApplicationBuilder(ClientApplication.class)
                .web(WebApplicationType.NONE)
                .run(args);
        try {
            context.getBean(ClientScenario.class).run();
            System.out.println("discovery-registry-ha kotlin e2e result=passed");
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
                .traceLogFile(logDir + "/client-" + Env.get("ZLINK_KOTLIN_E2E_SCENARIO") + "-flow.log")
                .traceLabel("kotlin-dr-client");
            for (String registry : Env.csv("ZLINK_KOTLIN_E2E_REGISTRY_ROUTERS")) {
                options.useDiscovery().addRegistryEndpoint(registry);
            }
            options.addClientServerChannel(Contracts.CHANNEL).enableClient();
        };
    }

    @Bean
    ZLinkRegistryQueryClient registryQueryClient(ZLinkBackendAdapterFactory backendAdapterFactory) {
        return ZLinkRemoteRegistryQueryClient.connect(
            queryRegistryEndpoint(),
            backendAdapterFactory);
    }

    private static String queryRegistryEndpoint() {
        String endpoint = Env.get("ZLINK_KOTLIN_E2E_QUERY_REGISTRY_ROUTER", "");
        if (endpoint.isBlank()) {
            java.util.List<String> registries = Env.csv("ZLINK_KOTLIN_E2E_REGISTRY_ROUTERS");
            if (!registries.isEmpty()) {
                endpoint = registries.get(0);
            }
        }
        return endpoint;
    }

    @Bean
    ClientScenario clientScenario(
        systems.zlink.framework.channels.ZLinkClient client,
        ObjectProvider<ZLinkRegistryQueryClient> registry,
        ObjectMapper json) {
        return new ClientScenario(client, registry.getIfAvailable(), json);
    }
}
