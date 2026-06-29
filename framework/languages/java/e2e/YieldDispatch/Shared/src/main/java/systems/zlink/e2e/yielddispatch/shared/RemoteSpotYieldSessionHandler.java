package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class RemoteSpotYieldSessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.RemoteSpotYieldRequest> {
    private final ZLinkRouteClient routes;

    public RemoteSpotYieldSessionHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "RemoteSpotYieldRequest";
    }

    @Override
    public Class<Contracts.RemoteSpotYieldRequest> messageType() {
        return Contracts.RemoteSpotYieldRequest.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.RemoteSpotYieldRequest request) {
        RoutingId targetNodeRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE));
        RoutingId targetSpotRid = RoutingId.from(dispatch.metadata()
            .getOrDefault(Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT));
        Contracts.ScenarioReply reply = routes
            .requestToSpot(
                Contracts.ROUTE_CHANNEL,
                targetNodeRid,
                targetSpotRid,
                request)
            .timeout(Duration.ofSeconds(30))
            .await(Contracts.ScenarioReply.class);
        context.client()
            .reply(reply)
            .await();
    }
}
