package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class EvidenceReqRouteHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.EvidenceReq> {
    private final ZLinkRouteClient routes;

    public EvidenceReqRouteHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public String packetName() {
        return "EvidenceReq";
    }

    @Override
    public Class<Contracts.EvidenceReq> messageType() {
        return Contracts.EvidenceReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.EvidenceReq request) {
        Contracts.EvidenceRes reply = routes
            .requestTo(
                Contracts.SPOT_MESH,
                SpotMsgRouteHandler.targetNode(dispatch),
                request)
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.EvidenceRes.class);
        context.client().reply(reply).await();
    }
}
