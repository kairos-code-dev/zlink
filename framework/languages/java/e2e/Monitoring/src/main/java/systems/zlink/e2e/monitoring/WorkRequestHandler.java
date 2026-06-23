package systems.zlink.e2e.monitoring;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class WorkRequestHandler
    implements ZLinkRequestHandler<Contracts.WorkRequest, Contracts.WorkReply> {
    @Override
    public Contracts.WorkReply handle(
        Contracts.WorkRequest request,
        ZLinkRequestContext context) {
        return new Contracts.WorkReply("work:" + request.value(), "svc-a");
    }
}
