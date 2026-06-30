package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class TimerStopMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.TimerStopMsg> {
    public TimerStopMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.TimerStopMsg.class);
    }

    @Override
    public String packetName() {
        return "TimerStopMsg";
    }
}
