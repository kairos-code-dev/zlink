package systems.zlink.samples.shoppingmall.server.orderworkflow.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.server.orderworkflow.WorkflowContinuationQueue;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderState;

@ZLinkHandlerGroup("workflow")
public final class StartOrderWorkflowHandler
    implements ZLinkRequestHandler<Messages.StartOrderWorkflowReq, Messages.StartOrderWorkflowRes> {
    private final OrderWorkflowService workflow;
    private final WorkflowContinuationQueue continuations;

    public StartOrderWorkflowHandler(
        OrderWorkflowService workflow, WorkflowContinuationQueue continuations) {
        this.workflow = workflow;
        this.continuations = continuations;
    }

    @Override
    public Messages.StartOrderWorkflowRes handle(
        Messages.StartOrderWorkflowReq request,
        ZLinkRequestContext context) {
        OrderState state = workflow.start(request);
        System.err.printf(
            "shoppingmall order: started order=%s status=%s%n",
            request.orderId(),
            state.status());
        continuations.enqueue(request.orderId());
        return new Messages.StartOrderWorkflowRes(state);
    }
}
