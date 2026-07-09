package systems.zlink.samples.shoppingmall.server.orderworkflow.spots.handlers;

import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.server.orderworkflow.spots.OrderWorkflowSpot;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

public final class ContinueOrderWorkflowSpotHandler
    implements ZLinkSpotRequestHandler<OrderWorkflowSpot, Messages.ContinueOrderWorkflowReq, Messages.ContinueOrderWorkflowRes> {
    private final OrderWorkflowService workflow;

    public ContinueOrderWorkflowSpotHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public Messages.ContinueOrderWorkflowRes handle(
        OrderWorkflowSpot spot,
        Messages.ContinueOrderWorkflowReq request) {
        return new Messages.ContinueOrderWorkflowRes(workflow.continueOrderInSpot(request.orderId()));
    }
}
