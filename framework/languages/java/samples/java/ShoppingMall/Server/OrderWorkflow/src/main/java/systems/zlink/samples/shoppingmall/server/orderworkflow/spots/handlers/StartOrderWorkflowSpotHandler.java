package systems.zlink.samples.shoppingmall.server.orderworkflow.spots.handlers;

import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.server.orderworkflow.spots.OrderWorkflowSpot;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

public final class StartOrderWorkflowSpotHandler
    implements ZLinkSpotRequestHandler<OrderWorkflowSpot, Messages.StartOrderWorkflowReq, Messages.StartOrderWorkflowRes> {
    private final OrderWorkflowService workflow;

    public StartOrderWorkflowSpotHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public Messages.StartOrderWorkflowRes handle(
        OrderWorkflowSpot spot,
        Messages.StartOrderWorkflowReq request) {
        return new Messages.StartOrderWorkflowRes(workflow.startInSpot(spot, request));
    }
}
