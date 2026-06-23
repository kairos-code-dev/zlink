package systems.zlink.e2e.registrymessaging.handlers;

import systems.zlink.e2e.registrymessaging.ScenarioState;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class ProfileCommandHandler
    implements ZLinkSendHandler<Contracts.ProfileCommand> {
    private final ScenarioState state;

    public ProfileCommandHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.ProfileCommand message,
        ZLinkSendContext context) {
        state.record("ProfileCommand", message.commandId());
    }
}
