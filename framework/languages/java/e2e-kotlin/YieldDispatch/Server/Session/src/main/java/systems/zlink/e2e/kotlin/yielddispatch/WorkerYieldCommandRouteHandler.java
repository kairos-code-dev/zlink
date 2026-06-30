package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteClient;

public final class WorkerYieldCommandRouteHandler
    extends SpotCommandRouteHandler<Contracts.WorkerYieldCommand> {
    public WorkerYieldCommandRouteHandler(ZLinkRouteClient routes) {
        super(routes, Contracts.WorkerYieldCommand.class);
    }

    @Override
    public String packetName() {
        return "WorkerYieldCommand";
    }
}
