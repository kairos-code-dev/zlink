package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ShutdownYieldSessionHandlers {
    private static final Duration ROUTE_REQUEST_TIMEOUT = Duration.ofSeconds(90);
    private static final Duration RECOVERY_REQUEST_TIMEOUT = Duration.ofSeconds(30);
    private static final Duration RECOVERY_PROBE_ATTEMPT_TIMEOUT = Duration.ofSeconds(5);

    private ShutdownYieldSessionHandlers() {
    }

    public static final class Wait
        implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.YieldShutdownScenarioReq> {
        private final ZLinkRouteClient routes;

        public Wait(ZLinkRouteClient routes) {
            this.routes = routes;
        }

        @Override
        public String packetName() {
            return "YieldShutdownScenarioReq";
        }

        @Override
        public Class<Contracts.YieldShutdownScenarioReq> messageType() {
            return Contracts.YieldShutdownScenarioReq.class;
        }

        @Override
        public void handle(
            ZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            Contracts.YieldShutdownScenarioReq request) {
            RoutingId playNode = RoutingId.from(Contracts.PLAY_NODE_A);
            RoutingId spotRid = RoutingId.from(request.spotRid());
            routes.requestTo(
                    Contracts.ROUTE_CHANNEL,
                    playNode,
                    new Contracts.EnsureSpotReq(request.spotRid()))
                .timeout(RECOVERY_REQUEST_TIMEOUT)
                .await(Contracts.EnsureSpotRes.class);
            routes.requestToSpot(
                    Contracts.ROUTE_CHANNEL,
                    playNode,
                    spotRid,
                    new Contracts.YieldReq("YD-E3", request.requestId(), "shutdown"))
                .metadata(Contracts.SPOT_RID_METADATA, request.spotRid())
                .timeout(ROUTE_REQUEST_TIMEOUT)
                .await(Contracts.ScenarioRes.class);
            context.client()
                .reply(new Contracts.YieldShutdownRes(
                    "yield.e3-shutdown-unexpected-completion",
                    request.requestId(),
                    request.spotRid()))
                .await();
        }
    }

    public static final class Recovery
        implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.YieldShutdownRecoveryReq> {
        private final ZLinkRouteClient routes;

        public Recovery(ZLinkRouteClient routes) {
            this.routes = routes;
        }

        @Override
        public String packetName() {
            return "YieldShutdownRecoveryReq";
        }

        @Override
        public Class<Contracts.YieldShutdownRecoveryReq> messageType() {
            return Contracts.YieldShutdownRecoveryReq.class;
        }

        @Override
        public void handle(
            ZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            Contracts.YieldShutdownRecoveryReq request) {
            RoutingId playNode = RoutingId.from(Contracts.PLAY_NODE_A);
            RoutingId spotRid = RoutingId.from(request.spotRid());
            routes.requestTo(
                    Contracts.ROUTE_CHANNEL,
                    playNode,
                    new Contracts.EnsureSpotReq(request.spotRid()))
                .timeout(RECOVERY_REQUEST_TIMEOUT)
                .await(Contracts.EnsureSpotRes.class);
            awaitProbeAfterRecovery(routes, playNode, spotRid, request.requestId());
            context.client()
                .reply(new Contracts.YieldShutdownRes(
                    "yield.e3-shutdown-recovery",
                    request.requestId(),
                    request.spotRid()))
                .await();
        }

        private static void awaitProbeAfterRecovery(
            ZLinkRouteClient routes,
            RoutingId playNode,
            RoutingId spotRid,
            String requestId) {
            RuntimeException lastError = null;
            for (int attempt = 0; attempt < 5; attempt++) {
                try {
                    routes.requestToSpot(
                            Contracts.ROUTE_CHANNEL,
                            playNode,
                            spotRid,
                            new Contracts.ProbeReq(requestId))
                        .timeout(RECOVERY_PROBE_ATTEMPT_TIMEOUT)
                        .await(Contracts.ProbeRes.class);
                    return;
                } catch (RuntimeException error) {
                    lastError = error;
                    try {
                        Thread.sleep(500);
                    } catch (InterruptedException interrupted) {
                        Thread.currentThread().interrupt();
                        throw new IllegalStateException("YD-E3 recovery probe interrupted", interrupted);
                    }
                }
            }
            throw lastError;
        }
    }
}
