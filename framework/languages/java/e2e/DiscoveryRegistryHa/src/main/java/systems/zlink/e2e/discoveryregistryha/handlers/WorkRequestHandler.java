package systems.zlink.e2e.discoveryregistryha.handlers;

import systems.zlink.e2e.discoveryregistryha.Contracts;
import systems.zlink.e2e.discoveryregistryha.ProviderState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class WorkRequestHandler
    implements ZLinkRequestHandler<Contracts.WorkRequest, Contracts.WorkReply> {
    private final ProviderState state;

    public WorkRequestHandler(ProviderState state) {
        this.state = state;
    }

    @Override
    public Contracts.WorkReply handle(
        Contracts.WorkRequest request,
        ZLinkRequestContext context) {
        return new Contracts.WorkReply("work:" + request.value(), state.providerRid());
    }
}
