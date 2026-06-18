package systems.zlink.samples.shoppingmall.server.orderworkflow.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderState;

@ZLinkHandlerGroup("workflow")
public final class RebuildOrderProjectionHandler
    implements ZLinkRequestHandler<Messages.RebuildOrderProjectionReq, Messages.RebuildOrderProjectionRes> {
    private final OrderWorkflowService workflow;

    public RebuildOrderProjectionHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public Messages.RebuildOrderProjectionRes handle(
        Messages.RebuildOrderProjectionReq request,
        ZLinkRequestContext context) {
        OrderState state = workflow.rebuildProjection(request.orderId());
        System.err.printf(
            "shoppingmall projection: rebuilt order=%s status=%s%n",
            request.orderId(),
            state.status());
        return new Messages.RebuildOrderProjectionRes(state);
    }
}
