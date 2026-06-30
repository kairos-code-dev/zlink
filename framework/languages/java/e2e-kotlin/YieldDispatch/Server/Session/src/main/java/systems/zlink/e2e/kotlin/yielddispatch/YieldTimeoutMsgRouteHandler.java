package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class YieldTimeoutMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.YieldTimeoutMsg> {
    public YieldTimeoutMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.YieldTimeoutMsg.class);
    }

    @Override
    public String packetName() {
        return "YieldTimeoutMsg";
    }
}
