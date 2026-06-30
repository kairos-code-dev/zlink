package systems.zlink.e2e.registrymessaging.workflow.Handlers;

import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.e2e.registrymessaging.workflow.Infrastructure.ScenarioState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class WorkflowRequestHandler
    implements ZLinkRequestHandler<Contracts.WorkflowRequest, Contracts.WorkflowReply> {
    private final ScenarioState state;

    public WorkflowRequestHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public Contracts.WorkflowReply handle(
        Contracts.WorkflowRequest request,
        ZLinkRequestContext context) {
        state.record("workflow-request", request.value());
        return new Contracts.WorkflowReply("workflow:" + request.value(), state.providerRid());
    }
}
