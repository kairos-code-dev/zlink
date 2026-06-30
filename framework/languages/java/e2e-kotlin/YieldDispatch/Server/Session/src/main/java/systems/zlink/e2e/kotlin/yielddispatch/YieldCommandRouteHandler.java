package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class YieldCommandRouteHandler
    extends SpotCommandRouteHandler<Contracts.YieldCommand> {
    public YieldCommandRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.YieldCommand.class);
    }

    @Override
    public String packetName() {
        return "YieldCommand";
    }
}
