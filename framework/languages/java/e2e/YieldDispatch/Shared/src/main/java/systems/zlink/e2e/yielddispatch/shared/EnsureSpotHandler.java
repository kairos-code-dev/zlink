package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class EnsureSpotHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.EnsureSpotRequest> {
    private final ZLinkRouteClient routes;

    public EnsureSpotHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "EnsureSpotRequest";
    }

    @Override
    public Class<Contracts.EnsureSpotRequest> messageType() {
        return Contracts.EnsureSpotRequest.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.EnsureSpotRequest request) {
        RoutingId targetNodeRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE));
        Contracts.EnsureSpotReply reply = routes
            .requestTo(
                Contracts.ROUTE_CHANNEL,
                targetNodeRid,
                request)
            .timeout(Duration.ofSeconds(30))
            .await(Contracts.EnsureSpotReply.class);
        context.client()
            .reply(reply)
            .await();
    }

    public static final class Play
        implements ZLinkRouteRequestHandler<Contracts.EnsureSpotRequest, Contracts.EnsureSpotReply> {
        private final ZLinkSpotManager spots;
        private final EvidenceStore evidence;

        public Play(
            ZLinkSpotManager spots,
            EvidenceStore evidence) {
            this.spots = spots;
            this.evidence = evidence;
        }

        @Override
        public Contracts.EnsureSpotReply handle(
            Contracts.EnsureSpotRequest request,
            ZLinkRouteRequestContext context) {
            spots.getOrCreate(
                    YieldProbeSpot.class,
                    RoutingId.from(request.spotRid()),
                    "ensure")
                .toCompletableFuture()
                .join();
            evidence.record("spot-ensured", request.spotRid(), "node=" + evidence.nodeRid());
            return new Contracts.EnsureSpotReply(request.spotRid(), evidence.nodeRid());
        }
    }
}
