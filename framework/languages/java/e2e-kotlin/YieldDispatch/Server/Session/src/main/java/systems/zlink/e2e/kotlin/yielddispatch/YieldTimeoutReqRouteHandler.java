package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class YieldTimeoutReqRouteHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.YieldTimeoutReq> {
    private final ZLinkRouteClient routes;

    public YieldTimeoutReqRouteHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "YieldTimeoutReq";
    }

    @Override
    public Class<Contracts.YieldTimeoutReq> messageType() {
        return Contracts.YieldTimeoutReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.YieldTimeoutReq request) {
        Contracts.YieldTimeoutRes reply = routes
            .requestToSpot(
                Contracts.SPOT_MESH,
                new SpotRef(
                    Contracts.SPOT_MESH,
                    SpotMsgRouteHandler.targetNode(dispatch),
                    SpotMsgRouteHandler.targetSpot(dispatch)),
                request)
            .timeout(Duration.ofSeconds(15))
            .await(Contracts.YieldTimeoutRes.class);
        context.client().reply(reply).await();
    }
}
