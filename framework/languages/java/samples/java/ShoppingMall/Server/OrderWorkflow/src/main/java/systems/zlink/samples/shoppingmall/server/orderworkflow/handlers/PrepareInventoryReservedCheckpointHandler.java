package systems.zlink.samples.shoppingmall.server.orderworkflow.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

@ZLinkHandlerGroup("order-workflow")
public final class PrepareInventoryReservedCheckpointHandler
    implements ZLinkRequestHandler<Messages.PrepareInventoryReservedCheckpointReq, Messages.ContinueOrderWorkflowRes> {
    private final OrderWorkflowService workflow;

    public PrepareInventoryReservedCheckpointHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public Messages.ContinueOrderWorkflowRes handle(
        Messages.PrepareInventoryReservedCheckpointReq request,
        ZLinkRequestContext context) {
        return new Messages.ContinueOrderWorkflowRes(workflow.prepareInventoryReserved(request.request()));
    }
}
