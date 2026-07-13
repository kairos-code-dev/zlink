package systems.zlink.samples.shoppingmall.server.orderworkflow.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import java.util.concurrent.CompletionStage;

@ZLinkHandlerGroup("order-workflow")
public final class ContinueOrderWorkflowHandler
    implements ZLinkRequestHandler<Messages.ContinueOrderWorkflowReq, Messages.ContinueOrderWorkflowRes> {
    private final OrderWorkflowService workflow;

    public ContinueOrderWorkflowHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public CompletionStage<Messages.ContinueOrderWorkflowRes> handle(
        Messages.ContinueOrderWorkflowReq request,
        ZLinkRequestContext context) {
        return workflow.continueOrder(request.orderId())
            .thenApply(Messages.ContinueOrderWorkflowRes::new);
    }
}
