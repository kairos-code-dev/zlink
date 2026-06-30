package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
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
        Contracts.EnsureSpotReply reply = routes
            .requestTo(
                Contracts.SPOT_MESH,
                SpotCommandRouteHandler.targetNode(dispatch),
                request)
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EnsureSpotReply.class);
        context.client().reply(reply).await();
    }
}
