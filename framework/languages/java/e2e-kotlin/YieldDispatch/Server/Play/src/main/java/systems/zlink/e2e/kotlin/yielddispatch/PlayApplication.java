package systems.zlink.e2e.kotlin.yielddispatch;

import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;
import systems.zlink.framework.spots.ZLinkSpotManager;

@EnableZLinkFramework
@SpringBootApplication(
    proxyBeanMethods = false,
    scanBasePackages = "systems.zlink.e2e.kotlin.yielddispatch.play")
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
            options.codecs().addJson();
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"));
            options.useRegistrySpotRemoteAddresses(Contracts.SPOT_MESH)
                .setRouterChannelId(Contracts.SPOT_MESH);
            RouteMeshChannelBuilder route = options.addRouteMesh(Contracts.SPOT_MESH)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_PLAY_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_SESSION_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            route.addRequestHandler(
                EvidenceRouteRequestHandler.class,
                Contracts.EvidenceRequest.class,
                Contracts.EvidenceReply.class);
            route.addRequestHandler(
                EnsureSpotRouteRequestHandler.class,
                Contracts.EnsureSpotRequest.class,
                Contracts.EnsureSpotReply.class);
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
    ApplicationRunner createSpots(ZLinkSpotManager spots) {
        return ignored -> {
            if (!"play-a".equals(Env.get("ZLINK_KOTLIN_E2E_NODE_RID", "play-a"))) {
                return;
            }
            spots.getOrCreate(ProbeSpot.class, RoutingId.from("room-a"), "bootstrap")
                .toCompletableFuture()
                .join();
            spots.getOrCreate(ProbeSpot.class, RoutingId.from("room-b"), "bootstrap")
                .toCompletableFuture()
                .join();
        };
    }

    @Bean
    PlayEvidenceStore evidence() {
        return new PlayEvidenceStore();
    }
}
