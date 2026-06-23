package systems.zlink.e2e.discoveryregistryha;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.discoveryregistryha.client")
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
            System.out.println("discovery-registry-ha e2e result=passed");
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
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/client-" + Env.get("ZLINK_JAVA_E2E_SCENARIO") + "-flow.log")
                .traceNodeId("java-dr-client");
            for (String registry : Env.csv("ZLINK_JAVA_E2E_REGISTRY_ROUTERS")) {
                options.useDiscovery().addRegistryEndpoint(registry);
            }
            options.addClientServerChannel(Contracts.CHANNEL).enableClient();
        };
    }

    @Bean
    ClientScenario clientScenario(
        systems.zlink.framework.channels.ZLinkClient client,
        ObjectMapper json) {
        return new ClientScenario(client, json);
    }
}
