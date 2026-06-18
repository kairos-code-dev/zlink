package systems.zlink.samples.shoppingmall.server.commerceapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

@ZLinkHandlerGroup("commerce")
public final class DeleteProjectionHandler
    implements ZLinkRequestHandler<Messages.DeleteProjectionReq, Messages.DeleteProjectionRes> {
    private final CommerceStore store;

    public DeleteProjectionHandler(CommerceStore store) {
        this.store = store;
    }

    @Override
    public Messages.DeleteProjectionRes handle(
        Messages.DeleteProjectionReq request,
        ZLinkRequestContext context) {
        return new Messages.DeleteProjectionRes(store.deleteReadModel(request.orderId()));
    }
}
