package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class SpotProbeMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.SpotProbeMsg> {
    public SpotProbeMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.SpotProbeMsg.class);
    }

    @Override
    public String packetName() {
        return "SpotProbeMsg";
    }
}
