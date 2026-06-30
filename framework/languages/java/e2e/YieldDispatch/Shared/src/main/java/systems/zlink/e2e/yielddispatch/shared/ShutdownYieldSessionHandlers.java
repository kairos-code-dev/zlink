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

    private ShutdownYieldSessionHandlers() {
    }

    public static final class Wait
        implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.YieldShutdownScenarioRequest> {
        private final ZLinkRouteClient routes;

        public Wait(ZLinkRouteClient routes) {
            this.routes = routes;
        }

        @Override
        public String packetName() {
            return "YieldShutdownScenarioRequest";
        }

        @Override
        public Class<Contracts.YieldShutdownScenarioRequest> messageType() {
            return Contracts.YieldShutdownScenarioRequest.class;
        }

        @Override
        public void handle(
            ZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            Contracts.YieldShutdownScenarioRequest request) {
            RoutingId playNode = RoutingId.from(Contracts.PLAY_NODE_A);
            RoutingId spotRid = RoutingId.from(request.spotRid());
            routes.requestTo(
                    Contracts.ROUTE_CHANNEL,
                    playNode,
                    new Contracts.EnsureSpotRequest(request.spotRid()))
                .timeout(RECOVERY_REQUEST_TIMEOUT)
                .await(Contracts.EnsureSpotReply.class);
            routes.requestToSpot(
                    Contracts.ROUTE_CHANNEL,
                    playNode,
                    spotRid,
                    new Contracts.YieldRequest("YD-E3", request.requestId(), "shutdown"))
                .metadata(Contracts.SPOT_RID_METADATA, request.spotRid())
                .timeout(ROUTE_REQUEST_TIMEOUT)
                .await(Contracts.ScenarioReply.class);
            context.client()
                .reply(new Contracts.YieldShutdownResult(
                    "yield.e3-shutdown-unexpected-completion",
                    request.requestId(),
                    request.spotRid()))
                .await();
        }
    }

    public static final class Recovery
        implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.YieldShutdownRecoveryRequest> {
        private final ZLinkRouteClient routes;

        public Recovery(ZLinkRouteClient routes) {
            this.routes = routes;
        }

        @Override
        public String packetName() {
            return "YieldShutdownRecoveryRequest";
        }

        @Override
        public Class<Contracts.YieldShutdownRecoveryRequest> messageType() {
            return Contracts.YieldShutdownRecoveryRequest.class;
        }

        @Override
        public void handle(
            ZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            Contracts.YieldShutdownRecoveryRequest request) {
            RoutingId playNode = RoutingId.from(Contracts.PLAY_NODE_A);
            RoutingId spotRid = RoutingId.from(request.spotRid());
            routes.requestTo(
                    Contracts.ROUTE_CHANNEL,
                    playNode,
                    new Contracts.EnsureSpotRequest(request.spotRid()))
                .timeout(RECOVERY_REQUEST_TIMEOUT)
                .await(Contracts.EnsureSpotReply.class);
            routes.requestToSpot(
                    Contracts.ROUTE_CHANNEL,
                    playNode,
                    spotRid,
                    new Contracts.ProbeRequest(request.requestId()))
                .timeout(RECOVERY_REQUEST_TIMEOUT)
                .await(Contracts.ProbeReply.class);
            context.client()
                .reply(new Contracts.YieldShutdownResult(
                    "yield.e3-shutdown-recovery",
                    request.requestId(),
                    request.spotRid()))
                .await();
        }
    }
}
