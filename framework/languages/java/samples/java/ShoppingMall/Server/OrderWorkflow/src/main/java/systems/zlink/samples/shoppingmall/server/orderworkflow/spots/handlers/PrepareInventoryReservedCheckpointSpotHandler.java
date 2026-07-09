package systems.zlink.samples.shoppingmall.server.orderworkflow.spots.handlers;

import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.server.orderworkflow.spots.OrderWorkflowSpot;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

public final class PrepareInventoryReservedCheckpointSpotHandler
    implements ZLinkSpotRequestHandler<OrderWorkflowSpot, Messages.PrepareInventoryReservedCheckpointReq, Messages.ContinueOrderWorkflowRes> {
    private final OrderWorkflowService workflow;

    public PrepareInventoryReservedCheckpointSpotHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public Messages.ContinueOrderWorkflowRes handle(
        OrderWorkflowSpot spot,
        Messages.PrepareInventoryReservedCheckpointReq request) {
        return new Messages.ContinueOrderWorkflowRes(workflow.prepareInventoryReservedInSpot(request.request()));
    }
}
