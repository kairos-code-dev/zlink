package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ProbeReqRouteHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ProbeReq> {
    private final ZLinkRouteClient routes;

    public ProbeReqRouteHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "ProbeReq";
    }

    @Override
    public Class<Contracts.ProbeReq> messageType() {
        return Contracts.ProbeReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.ProbeReq request) {
        Contracts.ProbeRes reply = routes
            .requestToSpot(
                Contracts.SPOT_MESH,
                new SpotRef(
                    Contracts.SPOT_MESH,
                    SpotMsgRouteHandler.targetNode(dispatch),
                    SpotMsgRouteHandler.targetSpot(dispatch)),
                request)
            .timeout(Duration.ofSeconds(15))
            .await(Contracts.ProbeRes.class);
        context.client().reply(reply).await();
    }
}
