package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class ProbeCommandRouteHandler
    extends SpotCommandRouteHandler<Contracts.ProbeCommand> {
    public ProbeCommandRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.ProbeCommand.class);
    }

    @Override
    public String packetName() {
        return "ProbeCommand";
    }
}
