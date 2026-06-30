package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class EvidenceRouteHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.EvidenceRequest> {
    private final ZLinkRouteClient routes;

    public EvidenceRouteHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "EvidenceRequest";
    }

    @Override
    public Class<Contracts.EvidenceRequest> messageType() {
        return Contracts.EvidenceRequest.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.EvidenceRequest request) {
        Contracts.EvidenceReply reply = routes
            .requestTo(
                Contracts.SPOT_MESH,
                SpotCommandRouteHandler.targetNode(dispatch),
                request)
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EvidenceReply.class);
        context.client().reply(reply).await();
    }
}
