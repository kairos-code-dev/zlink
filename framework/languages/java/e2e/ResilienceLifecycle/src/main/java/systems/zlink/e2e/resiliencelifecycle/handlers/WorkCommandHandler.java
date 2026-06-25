package systems.zlink.e2e.resiliencelifecycle.handlers;

import systems.zlink.e2e.resiliencelifecycle.Contracts;
import systems.zlink.e2e.resiliencelifecycle.ScenarioState;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class WorkCommandHandler implements ZLinkSendHandler<Contracts.WorkCommand> {
    private final ScenarioState state;

    public WorkCommandHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.WorkCommand message,
        ZLinkSendContext context) {
        state.record("WorkCommand", message.value());
    }
}
