package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class TimerStopRouteHandler
    extends SpotCommandRouteHandler<Contracts.TimerStopCommand> {
    public TimerStopRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.TimerStopCommand.class);
    }

    @Override
    public String packetName() {
        return "TimerStopCommand";
    }
}
