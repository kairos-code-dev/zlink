package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class TimerStartRouteHandler
    extends SpotCommandRouteHandler<Contracts.TimerStartCommand> {
    public TimerStartRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.TimerStartCommand.class);
    }

    @Override
    public String packetName() {
        return "TimerStartCommand";
    }
}
