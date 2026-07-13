package systems.zlink.samples.shoppingmall.server.orderworkflow.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.orderworkflow.OrderWorkflowService;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import java.util.concurrent.CompletionStage;

@ZLinkHandlerGroup("order-workflow")
public final class RebuildOrderProjectionHandler
    implements ZLinkRequestHandler<Messages.RebuildOrderProjectionReq, Messages.RebuildOrderProjectionRes> {
    private final OrderWorkflowService workflow;

    public RebuildOrderProjectionHandler(OrderWorkflowService workflow) {
        this.workflow = workflow;
    }

    @Override
    public CompletionStage<Messages.RebuildOrderProjectionRes> handle(
        Messages.RebuildOrderProjectionReq request,
        ZLinkRequestContext context) {
        return workflow.rebuildProjection(request.orderId())
            .thenApply(Messages.RebuildOrderProjectionRes::new);
    }
}
