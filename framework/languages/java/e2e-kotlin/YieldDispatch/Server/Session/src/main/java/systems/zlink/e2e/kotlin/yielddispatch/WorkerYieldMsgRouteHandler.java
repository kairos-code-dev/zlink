package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class WorkerYieldMsgRouteHandler
    extends SpotMsgRouteHandler<Contracts.WorkerYieldMsg> {
    public WorkerYieldMsgRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.WorkerYieldMsg.class);
    }

    @Override
    public String packetName() {
        return "WorkerYieldMsg";
    }
}
