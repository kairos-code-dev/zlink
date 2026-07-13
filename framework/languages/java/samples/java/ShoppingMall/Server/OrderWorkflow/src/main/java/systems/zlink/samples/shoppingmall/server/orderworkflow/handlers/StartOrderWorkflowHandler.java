package systems.zlink.samples.shoppingmall.server.orderworkflow.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import java.util.concurrent.CompletionStage;

@ZLinkHandlerGroup("order-workflow")
public final class StartOrderWorkflowHandler
    implements ZLinkRequestHandler<Messages.StartOrderWorkflowReq, Messages.StartOrderWorkflowRes> {
    private final OrderWorkflowService workflow;

    public StartOrderWorkflowHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public CompletionStage<Messages.StartOrderWorkflowRes> handle(
        Messages.StartOrderWorkflowReq request,
        ZLinkRequestContext context) {
        return workflow.start(request).thenApply(Messages.StartOrderWorkflowRes::new);
    }
}
