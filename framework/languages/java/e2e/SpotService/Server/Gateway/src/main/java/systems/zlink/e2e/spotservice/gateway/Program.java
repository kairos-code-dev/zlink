package systems.zlink.e2e.spotservice.gateway;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.spotservice.shared.ClientDriverSpot;
import systems.zlink.e2e.spotservice.shared.Contracts;
import systems.zlink.e2e.spotservice.shared.Env;
import systems.zlink.e2e.spotservice.shared.GatewayScenarioHttpServer;
import systems.zlink.e2e.spotservice.shared.ScenarioState;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spots.ZLinkSpotManager;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotservice.gateway")
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
    ScenarioState scenarioState() {
        return new ScenarioState("gateway");
    }

    @Bean
    GatewayScenarioHttpServer gatewayScenarioHttpServer(ZLinkSpotManager spots) {
        return new GatewayScenarioHttpServer(Env.get("ZLINK_JAVA_E2E_GATEWAY_HTTP_ENDPOINT"), spots);
    }

    @Bean
    systems.zlink.framework.spring.ZLinkFrameworkConfigurer gatewayFramework() {
        return options -> {
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            String gatewayRid = Env.get("ZLINK_JAVA_E2E_GATEWAY_RID", "client-route-mesh");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/gateway-flow.log")
                .traceLabel("java-sm-gateway");
            boolean spotOnly = "true".equalsIgnoreCase(
                Env.get("ZLINK_JAVA_E2E_SPOT_ONLY", ""));
            if (spotOnly) {
                System.out.println("[topology] role=gateway route_mesh=disabled");
            } else {
                options.addRouteMeshChannel(Contracts.ROUTE_CHANNEL)
                    .enableServer(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                    .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT"))
                    .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT"))
                    .setRoutingId(RoutingId.from(gatewayRid));
            }
            options.addClientServerChannel(Contracts.EGRESS_CHANNEL)
                .enableClient(Env.get("ZLINK_JAVA_E2E_INGRESS_A_ENDPOINT"));
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH);
            node.enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(gatewayRid));
            node.addSpotFactory(ClientDriverSpot.class);
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")));
    }
}
