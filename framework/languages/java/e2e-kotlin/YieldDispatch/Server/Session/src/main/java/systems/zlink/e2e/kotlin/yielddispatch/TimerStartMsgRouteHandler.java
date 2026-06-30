package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class TimerStartMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.TimerStartMsg> {
    public TimerStartMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.TimerStartMsg.class);
    }

    @Override
    public String packetName() {
        return "TimerStartMsg";
    }
}
