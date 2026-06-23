package systems.zlink.e2e.registrymessaging;

import java.time.Duration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.registrymessaging.client")
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
            System.out.println("registry-messaging e2e result=passed");
        } finally {
            context.close();
        }
    }

    @Bean
    ZLinkFrameworkConfigurer clientFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/client-flow.log")
                .traceNodeId("java-rm-client");
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
            options.addClientServerChannel(Contracts.API_CHANNEL).enableClient();
            options.addClientServerChannel(Contracts.WORKFLOW_CHANNEL).enableClient();
            options.addClientServerChannel("registry.messaging.api.manual")
                .enableClient(Env.get("ZLINK_JAVA_E2E_API_A_ENDPOINT"));
            options.addClientServerChannel("registry.messaging.api.manual.multi")
                .enableClient(Env.get("ZLINK_JAVA_E2E_API_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_API_B_ENDPOINT"));
            options.addRouteMeshChannel(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_CLIENT_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from("client"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT"))
                .setDefaultRequestTimeout(Duration.ofSeconds(2));
        };
    }

    @Bean
    ClientScenario clientScenario(
        systems.zlink.framework.channels.ZLinkClient client,
        systems.zlink.framework.channels.ZLinkRouteClient routes) {
        return new ClientScenario(client, routes);
    }
}
