package systems.zlink.e2e.runtimemonitoring.service.handlers;

import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class WorkReqHandler
    implements ZLinkRequestHandler<Contracts.WorkReq, Contracts.WorkRes> {
    @Override
    public Contracts.WorkRes handle(
        Contracts.WorkReq request,
        ZLinkRequestContext context) {
        return new Contracts.WorkRes("work:" + request.value(), "svc-a");
    }
}
