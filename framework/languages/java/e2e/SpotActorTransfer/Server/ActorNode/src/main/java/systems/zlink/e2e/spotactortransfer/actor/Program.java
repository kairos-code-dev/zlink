package systems.zlink.e2e.spotactortransfer.actor;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.nio.file.Path;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.core.env.StandardEnvironment;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.spotactortransfer.shared.Contracts;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@EnableConfigurationProperties(ActorNodeOptions.class)
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotactortransfer.actor")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        String config = configPath(args);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .environment(isolatedEnvironment())
            .properties("spring.config.location=" + Path.of(config).toAbsolutePath().toUri())
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run();
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceStore evidenceStore(ActorNodeOptions config) {
        return new EvidenceStore(config.nodeRid(),
            config.logDirectory() + "/" + config.nodeRid() + ".evidence.log");
    }

    @Bean
    GateStore gateStore() {
        return new GateStore();
    }

    @Bean
    DomainStateStore domainStateStore(ActorNodeOptions config) {
        return new DomainStateStore(config.logDirectory() + "/domain-state");
    }

    @Bean
    ActorNodeHttpServer actorNodeHttpServer(
        ObjectMapper json,
        EvidenceStore evidence,
        GateStore gates,
        systems.zlink.framework.spots.ZLinkSpotManager spots,
        systems.zlink.framework.actors.ZLinkActorManager actors,
        systems.zlink.framework.actors.ZLinkActorClient actorClient,
        ActorNodeOptions config) {
        return new ActorNodeHttpServer(
            config.httpEndpoint(),
            json,
            evidence,
            gates,
            spots,
            actors,
            actorClient);
    }

    @Bean
    ZLinkFrameworkConfigurer framework(EvidenceStore evidence, ActorNodeOptions config) {
        return options -> {
            String nodeRid = evidence.nodeRid();
            options.addHandlersFromPackageOf(TransferComponents.class);
            options.setActorTransferForwardWindow(Duration.ofSeconds(2));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .setMessageFlowObserver(flow -> {
                    evidence.addFlow(flow);
                    return java.util.concurrent.CompletableFuture.completedFuture(null);
                })
                .traceLogFile(config.logDirectory() + "/" + nodeRid + "-flow.log")
                .traceLabel("java-spot-transfer-" + nodeRid);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.MESH);
            node.enableRouter(config.routerEndpoint())
                .setRoutingId(RoutingId.from(nodeRid));
            for (String peer : config.routerPeers().split(",")) {
                String[] fields = peer.split("=", 2);
                if (fields.length == 2 && !nodeRid.equals(fields[0])) {
                    node.connectRouter(RoutingId.from(fields[0]), fields[1]);
                }
            }
            node.configureEntrySpot().setRoutingId(RoutingId.from(Contracts.ENTRY_SPOT_RID));
            node.addEntrySpot(TransferComponents.TransferEntrySpot.class);
            registerActor(node, Contracts.STATEFUL, true);
            registerActor(node, Contracts.EMPTY_STATE, true);
            registerActor(node, Contracts.NO_ADAPTER, false);
            registerActor(node, Contracts.FAIL_OUT, true);
            registerActor(node, Contracts.FAIL_LEAVE, true);
            registerActor(node, Contracts.FAIL_IN, true);
            node.addSpotFactory(TransferComponents.TransferUserSpot.class);
            options.addStreamNode("spot-transfer-session-" + nodeRid)
                .bind(config.streamEndpoint())
                .registerSession(TransferComponents.TransferSession.class);
        };
    }

    private static void registerActor(
        ZLinkSpotNodeBuilder node,
        String actorType,
        boolean adapter) {
        node.addActorFactory(actorType, TransferComponents.TransferActorFactory.class);
        if (adapter) {
            node.addActorTransferAdapter(actorType, TransferComponents.TransferActorAdapter.class);
        }
    }

    @Bean
    ZLinkRedisLocationStore locationStore(ActorNodeOptions config) {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(config.redisLocationEndpoint())
            .setKeyPrefix(config.locationKeyPrefix()));
    }

    private static String configPath(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: spot-actor-transfer-node --config <path>");
        }
        return args[1];
    }

    private static StandardEnvironment isolatedEnvironment() {
        StandardEnvironment value = new StandardEnvironment();
        value.getPropertySources().remove(StandardEnvironment.SYSTEM_ENVIRONMENT_PROPERTY_SOURCE_NAME);
        value.getPropertySources().remove(StandardEnvironment.SYSTEM_PROPERTIES_PROPERTY_SOURCE_NAME);
        return value;
    }
}
