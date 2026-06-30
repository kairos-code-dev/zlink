package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class YieldTimeoutRouteHandler
    extends SpotCommandRouteHandler<Contracts.YieldTimeoutCommand> {
    public YieldTimeoutRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.YieldTimeoutCommand.class);
    }

    @Override
    public String packetName() {
        return "YieldTimeoutCommand";
    }
}
