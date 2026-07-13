package systems.zlink.e2e.kotlin.automaticturn;

import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions;
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.ZLinkSpotManager;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.kotlin.automaticturn.play")
public final class PlayApplication {
    private PlayApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(PlayApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        return options -> {
            String nodeRid = Env.get("ZLINK_KOTLIN_E2E_NODE_RID", "play-a");
            String logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs");
            options.configureDispatch()
                .messageFlow(ZLinkMessageFlowLogMode.KEY_TRANSITIONS)
                .traceLogFile(logDir + "/" + nodeRid + "-flow.log")
                .traceLabel("kotlin-atd-" + nodeRid);
            RouteMeshChannelBuilder route = options.addRouteMeshChannel(Contracts.SPOT_MESH)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_PLAY_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_SESSION_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            route.addRequestHandler(
                EvidenceRouteRequestHandler.class,
                Contracts.EvidenceReq.class,
                Contracts.EvidenceRes.class);
            route.addRequestHandler(
                EnsureSpotRouteRequestHandler.class,
                Contracts.EnsureSpotReq.class,
                Contracts.EnsureSpotRes.class);
            route.addRequestHandler(
                PlayBindActorsHandler.class,
                Contracts.BindActorsReq.class,
                Contracts.BindActorsRes.class);
            options.addClientServerChannel(Contracts.DELAY_CHANNEL)
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_DELAY_ENDPOINT"));
            ZLinkSpotNodeBuilder spot = options.addSpotMesh(Contracts.SPOT_MESH)
                .enableRouter(Env.get("ZLINK_KOTLIN_E2E_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            spot.addEntrySpot(ProbeEntrySpot.class);
            spot.addSpotFactory(ProbeSpot.class);
            spot.addActorFactory("probe", ProbeActorFactory.class);
        };
    }

    @Bean
    ZLinkRedisLocationStore locationStore() {
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(Env.get("ZLINK_KOTLIN_E2E_REDIS_LOCATION_ENDPOINT"))
            .setKeyPrefix(Env.get("ZLINK_KOTLIN_E2E_LOCATION_KEY_PREFIX")));
    }

    @Bean
    ApplicationRunner createSpots(ZLinkSpotManager spots) {
        return ignored -> {
            if (!"play-a".equals(Env.get("ZLINK_KOTLIN_E2E_NODE_RID", "play-a"))) {
                return;
            }
            spots.getOrCreate(ProbeSpot.class, RoutingId.from("room-a"), ZLinkMessage.of("bootstrap"))
                .toCompletableFuture()
                .join();
            spots.getOrCreate(ProbeSpot.class, RoutingId.from("room-b"), ZLinkMessage.of("bootstrap"))
                .toCompletableFuture()
                .join();
        };
    }

    @Bean
    PlayEvidenceStore evidence() {
        return new PlayEvidenceStore();
    }
}
