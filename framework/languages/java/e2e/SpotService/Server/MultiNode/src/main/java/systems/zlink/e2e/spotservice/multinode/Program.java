package systems.zlink.e2e.spotservice.multinode;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.spotservice.shared.Contracts;
import systems.zlink.e2e.spotservice.shared.Env;
import systems.zlink.e2e.spotservice.shared.MultiNodeSpot;
import systems.zlink.e2e.spotservice.shared.ScenarioActorFactory;
import systems.zlink.e2e.spotservice.shared.ScenarioEntrySpot;
import systems.zlink.e2e.spotservice.shared.ScenarioState;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotservice.multinode")
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
        return new ScenarioState(Env.get("ZLINK_JAVA_E2E_NODE_RID", "multi-node-a"));
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    MultiNodeHttpServer multiNodeHttpServer(
        ScenarioState state,
        ObjectMapper json,
        systems.zlink.framework.spots.ZLinkSpotManager spots,
        systems.zlink.framework.channels.ZLinkRouteClient routes,
        ZLinkActorManager actors,
        ZLinkActorClient actorClient,
        systems.zlink.framework.spots.SpotHandleResolver spotHandles) {
        return new MultiNodeHttpServer(
            state,
            json,
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            spots,
            routes,
            actors,
            actorClient,
            spotHandles);
    }

    @Bean
    ZLinkFrameworkConfigurer multiNodeFramework(ScenarioState state) {
        return options -> {
            String nodeRid = state.nodeRid();
            String logDir = Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + nodeRid + "-flow.log")
                .traceLabel("java-sm-" + nodeRid);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.SPOT_MESH);
            node
                .enableRouter(Env.get("ZLINK_JAVA_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            node.addEntrySpot(ScenarioEntrySpot.class);
            node.addActorFactory("scenario", ScenarioActorFactory.class);
            node.addSpotFactory(MultiNodeSpot.class);
            if (!"true".equalsIgnoreCase(Env.get("ZLINK_JAVA_E2E_SPOT_ONLY", ""))) {
                options.addRouteMeshChannel(Contracts.ROUTE_CHANNEL)
                    .enableServer(Env.get("ZLINK_JAVA_E2E_ROUTE_ENDPOINT"))
                    .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_A_ENDPOINT"))
                    .enableClient(Env.get("ZLINK_JAVA_E2E_ROUTE_B_ENDPOINT"))
                    .setRoutingId(RoutingId.from(nodeRid));
            } else {
                System.out.println("[topology] role=" + nodeRid + " route_mesh=disabled");
            }
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_JAVA_E2E_LOCATION_KEY_PREFIX")));
    }
}
