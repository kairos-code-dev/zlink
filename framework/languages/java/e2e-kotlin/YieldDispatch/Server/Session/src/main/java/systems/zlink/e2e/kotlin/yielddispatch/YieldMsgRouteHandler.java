package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class YieldMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.YieldMsg> {
    public YieldMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.YieldMsg.class);
    }

    @Override
    public String packetName() {
        return "YieldMsg";
    }
}
