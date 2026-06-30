package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class ProbeMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.ProbeMsg> {
    public ProbeMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.ProbeMsg.class);
    }

    @Override
    public String packetName() {
        return "ProbeMsg";
    }
}
