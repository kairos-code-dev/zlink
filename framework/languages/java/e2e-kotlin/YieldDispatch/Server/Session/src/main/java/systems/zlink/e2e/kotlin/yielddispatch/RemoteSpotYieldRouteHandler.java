package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class RemoteSpotYieldRouteHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.RemoteSpotYieldRequest> {
    private final ZLinkRouteClient routes;

    public RemoteSpotYieldRouteHandler(ZLinkRouteClient routes) {
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
        Contracts.ScenarioReply reply = routes
            .requestToSpot(
                Contracts.SPOT_MESH,
                SpotCommandRouteHandler.targetNode(dispatch),
                SpotCommandRouteHandler.targetSpot(dispatch),
                request)
            .timeout(Duration.ofSeconds(10))
            .await(Contracts.ScenarioReply.class);
        context.client().reply(reply).await();
    }
}
