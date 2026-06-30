package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class HoldMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.HoldMsg> {
    public HoldMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.HoldMsg.class);
    }

    @Override
    public String packetName() {
        return "HoldMsg";
    }
}
