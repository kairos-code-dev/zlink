package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class HoldCommandRouteHandler
    extends SpotCommandRouteHandler<Contracts.HoldCommand> {
    public HoldCommandRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.HoldCommand.class);
    }

    @Override
    public String packetName() {
        return "HoldCommand";
    }
}
