package systems.zlink.e2e.spotactortransfer.actor;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.e2e.spotactortransfer.shared.Contracts;
import systems.zlink.e2e.spotactortransfer.shared.Env;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.spotactortransfer.actor")
public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        Env.configure(args);
        SpringApplicationBuilder builder = new SpringApplicationBuilder(Program.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        builder.run();
    }

    @Bean
    ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean
    EvidenceStore evidenceStore() {
        String nodeRid = Env.require("nodeRid");
        String logDir = Env.require("logDirectory");
        return new EvidenceStore(nodeRid, logDir + "/" + nodeRid + ".evidence.log");
    }

    @Bean
    GateStore gateStore() {
        return new GateStore();
    }

    @Bean
    DomainStateStore domainStateStore() {
        return new DomainStateStore(Env.require("logDirectory") + "/domain-state");
    }

    @Bean
    ActorNodeHttpServer actorNodeHttpServer(
        ObjectMapper json,
        EvidenceStore evidence,
        GateStore gates,
        systems.zlink.framework.spots.ZLinkSpotManager spots,
        systems.zlink.framework.actors.ZLinkActorManager actors,
        systems.zlink.framework.actors.ZLinkActorClient actorClient) {
        return new ActorNodeHttpServer(
            Env.require("httpEndpoint"),
            json,
            evidence,
            gates,
            spots,
            actors,
            actorClient);
    }

    @Bean
    ZLinkFrameworkConfigurer framework(EvidenceStore evidence) {
        return options -> {
            String nodeRid = evidence.nodeRid();
            String logDir = Env.require("logDirectory");
            options.addHandlersFromPackageOf(TransferComponents.class);
            options.setActorTransferForwardWindow(Duration.ofSeconds(2));
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .setMessageFlowObserver(flow -> {
                    evidence.addFlow(flow);
                    return java.util.concurrent.CompletableFuture.completedFuture(null);
                })
                .traceLogFile(logDir + "/" + nodeRid + "-flow.log")
                .traceLabel("java-spot-transfer-" + nodeRid);
            ZLinkSpotNodeBuilder node = options.addSpotMesh(Contracts.MESH);
            node.enableRouter(Env.require("routerEndpoint"))
                .setRoutingId(RoutingId.from(nodeRid));
            for (String peer : Env.require("routerPeers").split(",")) {
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
                .bind(Env.require("streamEndpoint"))
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
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.require("redisLocationEndpoint"))
            .setKeyPrefix(Env.require("locationKeyPrefix")));
    }
}
