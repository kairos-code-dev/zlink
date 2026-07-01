package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class YieldCancelMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.YieldCancelMsg> {
    public YieldCancelMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.YieldCancelMsg.class);
    }

    @Override
    public String packetName() {
        return "YieldCancelMsg";
    }
}
