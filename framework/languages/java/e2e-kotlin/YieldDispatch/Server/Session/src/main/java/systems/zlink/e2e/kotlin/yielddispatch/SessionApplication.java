package systems.zlink.e2e.kotlin.yielddispatch;

import org.springframework.boot.WebApplicationType;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.context.annotation.Bean;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.spring.EnableZLinkFramework;
import systems.zlink.framework.spring.ZLinkFrameworkConfigurer;

@EnableZLinkFramework
@SpringBootApplication(proxyBeanMethods = false)
public final class SessionApplication {
    private SessionApplication() {
    }

    public static AutoCloseable run(String... args) {
        SpringApplicationBuilder builder = new SpringApplicationBuilder(SessionApplication.class)
            .web(WebApplicationType.NONE);
        builder.application().setKeepAlive(true);
        return builder.run(args)::close;
    }

    @Bean
    ZLinkFrameworkConfigurer framework() {
        return options -> {
            String nodeRid = Env.get("ZLINK_KOTLIN_E2E_NODE_RID", "session-a");
            options.useDiscovery().addRegistryEndpoint(Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"));
            options.useRegistrySpotRemoteAddresses(Contracts.SPOT_MESH)
                .setRouterChannelId(Contracts.SPOT_MESH);
            var route = options.addRouteMesh(Contracts.SPOT_MESH)
                .enableServer(Env.get("ZLINK_KOTLIN_E2E_SESSION_ROUTE_ENDPOINT"))
                .enableClient(Env.get("ZLINK_KOTLIN_E2E_PLAY_ROUTE_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            String playBRoute = Env.get("ZLINK_KOTLIN_E2E_PLAY_B_ROUTE_ENDPOINT", "");
            if (!playBRoute.isBlank()) {
                route.enableClient(playBRoute);
            }
            ZLinkSpotNodeBuilder spot = options.addSpotMesh(Contracts.SPOT_MESH)
                .enableRouter(Env.get("ZLINK_KOTLIN_E2E_SESSION_SPOT_ENDPOINT"))
                .setRoutingId(RoutingId.from(nodeRid));
            spot.addEntrySpot(ProbeEntrySpot.class);
            spot.addActorFactory("probe", ProbeActorFactory.class);
            options.addStreamNode("gateway")
                .bind(Env.get("ZLINK_KOTLIN_E2E_STREAM_ENDPOINT"))
                .registerSession(ProbeSession.class)
                .addSessionPacketHandler(ActorAuthReqHandler.class)
                .addSessionPacketHandler(BindActorsReqHandler.class)
                .addSessionPacketHandler(EnsureSpotReqHandler.class)
                .addSessionPacketHandler(RemoteSpotYieldReqRouteHandler.class)
                .addSessionPacketHandler(HoldMsgRouteHandler.class)
                .addSessionPacketHandler(YieldMsgRouteHandler.class)
                .addSessionPacketHandler(WorkerYieldMsgRouteHandler.class)
                .addSessionPacketHandler(ProbeMsgRouteHandler.class)
                .addSessionPacketHandler(TimerStartMsgRouteHandler.class)
                .addSessionPacketHandler(TimerStopMsgRouteHandler.class)
                .addSessionPacketHandler(YieldTimeoutMsgRouteHandler.class)
                .addSessionPacketHandler(YieldCancelMsgRouteHandler.class)
                .addSessionPacketHandler(SpotProbeMsgRouteHandler.class)
                .addSessionPacketHandler(EvidenceReqRouteHandler.class);
        };
    }
}
