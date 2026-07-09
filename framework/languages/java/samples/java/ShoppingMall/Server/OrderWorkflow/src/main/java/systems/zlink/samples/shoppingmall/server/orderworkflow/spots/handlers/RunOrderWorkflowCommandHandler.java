package systems.zlink.samples.shoppingmall.server.orderworkflow.spots.handlers;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.server.orderworkflow.spots.OrderWorkflowSpot;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

public final class RunOrderWorkflowCommandHandler
    implements ZLinkSpotPacketHandler<OrderWorkflowSpot, Messages.RunOrderWorkflowCommand> {
    private final OrderWorkflowService workflow;

    public RunOrderWorkflowCommandHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public void handle(
        OrderWorkflowSpot spot,
        Messages.RunOrderWorkflowCommand message) {
        workflow.continueOrderInSpot(message.orderId());
    }
}
