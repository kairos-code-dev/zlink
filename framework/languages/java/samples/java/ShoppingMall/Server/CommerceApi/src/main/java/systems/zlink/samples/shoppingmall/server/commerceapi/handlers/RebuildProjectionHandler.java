package systems.zlink.samples.shoppingmall.server.commerceapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.commerceapi.OrderWorkflowRouter;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderState;

/**
 * Relays projection rebuild to the {@code OrderWorkflow} owner; CommerceApi
 * never rebuilds the store itself.
 */
@ZLinkHandlerGroup("commerce")
public final class RebuildProjectionHandler
    implements ZLinkRequestHandler<Messages.RebuildProjectionApiReq, Messages.RebuildProjectionApiRes> {
    private final OrderWorkflowRouter workflows;

    public RebuildProjectionHandler(OrderWorkflowRouter workflows) {
        this.workflows = workflows;
    }

    @Override
    public Messages.RebuildProjectionApiRes handle(
        Messages.RebuildProjectionApiReq request,
        ZLinkRequestContext context) {
        OrderState state = workflows.rebuildProjection(request.orderId());
        return new Messages.RebuildProjectionApiRes(state);
    }
}
