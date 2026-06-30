package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class EnsureSpotReqHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.EnsureSpotReq> {
    private final ZLinkRouteClient routes;

    public EnsureSpotReqHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "EnsureSpotReq";
    }

    @Override
    public Class<Contracts.EnsureSpotReq> messageType() {
        return Contracts.EnsureSpotReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.EnsureSpotReq request) {
        Contracts.EnsureSpotRes reply = routes
            .requestTo(
                Contracts.SPOT_MESH,
                SpotMsgRouteHandler.targetNode(dispatch),
                request)
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EnsureSpotRes.class);
        context.client().reply(reply).await();
    }
}
