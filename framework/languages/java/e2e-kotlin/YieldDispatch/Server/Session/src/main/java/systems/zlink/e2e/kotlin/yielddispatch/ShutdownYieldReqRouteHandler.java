package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ShutdownYieldReqRouteHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ShutdownYieldReq> {
    private final ZLinkRouteClient routes;

    public ShutdownYieldReqRouteHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "ShutdownYieldReq";
    }

    @Override
    public Class<Contracts.ShutdownYieldReq> messageType() {
        return Contracts.ShutdownYieldReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.ShutdownYieldReq request) {
        Contracts.ScenarioRes reply = routes
            .requestToSpot(
                Contracts.SPOT_MESH,
                new SpotRef(
                    Contracts.SPOT_MESH,
                    SpotMsgRouteHandler.targetNode(dispatch),
                    SpotMsgRouteHandler.targetSpot(dispatch)),
                request)
            .timeout(Duration.ofSeconds(20))
            .await(Contracts.ScenarioRes.class);
        context.client().reply(reply).await();
    }
}
