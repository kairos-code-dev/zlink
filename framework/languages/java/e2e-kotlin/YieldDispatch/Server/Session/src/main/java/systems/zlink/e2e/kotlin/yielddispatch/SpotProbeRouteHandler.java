package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class SpotProbeRouteHandler
    extends SpotCommandRouteHandler<Contracts.SpotProbeCommand> {
    public SpotProbeRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.SpotProbeCommand.class);
    }

    @Override
    public String packetName() {
        return "SpotProbeCommand";
    }
}
