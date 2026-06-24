package systems.zlink.e2e.spotservice;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotservice.client")
public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) {
        ConfigurableApplicationContext context =
            new SpringApplicationBuilder(ClientApplication.class)
                .web(WebApplicationType.NONE)
                .run(args);
        try {
            systems.zlink.framework.spots.ZLinkSpotManager spots =
                context.getBean(systems.zlink.framework.spots.ZLinkSpotManager.class);
            String mode = Env.get("ZLINK_JAVA_E2E_CLIENT_MODE", "state1");
            ClientDriverSpot.configure(mode);
            spots.create(ClientDriverSpot.class, RoutingId.from("client-driver-" + mode))
                .toCompletableFuture()
                .join();
            ClientDriverSpot.awaitResult();
            System.out.println("spot-service e2e mode=" + mode + " result=passed");
        } finally {
            context.close();
        }
    }

    @Bean
    ScenarioState scenarioState() {
        return new ScenarioState("client");
    }

    @Bean
    systems.zlink.framework.spring.ZLinkFrameworkConfigurer clientFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.codecs().addJson();
            options.addSpotRemoteAddressResolver(SpotRouteResolver.class);
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/client-flow.log")
                .traceNodeId("java-sm-client");
            options.addRouteMesh(Contracts.ROUTE_CHANNEL)
                .enableServer(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT"))
                .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT"))
                .setRoutingId(RoutingId.from("client"));
            options.addClientServerChannel(Contracts.EGRESS_CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_INGRESS_A_ENDPOINT"));
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH);
            node.enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                .setRouterRoutingId(RoutingId.from("client"));
            node.addSpotFactory(ClientDriverSpot.class);
        };
    }
}
