package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class RemoteSpotYieldReqRouteHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.RemoteSpotYieldReq> {
    private final ZLinkRouteClient routes;

    public RemoteSpotYieldReqRouteHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "RemoteSpotYieldReq";
    }

    @Override
    public Class<Contracts.RemoteSpotYieldReq> messageType() {
        return Contracts.RemoteSpotYieldReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.RemoteSpotYieldReq request) {
        Contracts.ScenarioRes reply = routes
            .requestToSpot(
                Contracts.SPOT_MESH,
                new SpotRef(
                    Contracts.SPOT_MESH,
                    SpotMsgRouteHandler.targetNode(dispatch),
                    SpotMsgRouteHandler.targetSpot(dispatch)),
                request)
            .timeout(Duration.ofSeconds(10))
            .await(Contracts.ScenarioRes.class);
        context.client().reply(reply).await();
    }
}
