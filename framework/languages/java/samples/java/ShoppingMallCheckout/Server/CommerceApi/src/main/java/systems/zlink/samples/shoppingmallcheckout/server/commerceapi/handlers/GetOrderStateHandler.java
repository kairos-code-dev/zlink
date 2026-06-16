package systems.zlink.samples.shoppingmallcheckout.server.commerceapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmallcheckout.server.configuration.CommerceStore;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages.OrderState;

/**
 * Read-only projection lookup. Never advances the workflow or appends events.
 */
@ZLinkHandlerGroup("commerce")
public final class GetOrderStateHandler
    implements ZLinkRequestHandler<Messages.GetOrderStateReq, Messages.GetOrderStateRes> {
    private final CommerceStore store;

    public GetOrderStateHandler(CommerceStore store) {
        this.store = store;
    }

    @Override
    public Messages.GetOrderStateRes handle(
        Messages.GetOrderStateReq request,
        ZLinkRequestContext context) {
        OrderState state = store.findReadModel(request.orderId())
            .orElseThrow(() -> new IllegalStateException(
                "Order '" + request.orderId() + "' does not exist."));
        return new Messages.GetOrderStateRes(state);
    }
}
