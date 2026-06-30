package systems.zlink.e2e.registrymessaging.provider.Handlers;

import systems.zlink.e2e.registrymessaging.provider.Infrastructure.ScenarioState;
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
        if (message.commandId().startsWith("slow")) {
            try {
                Thread.sleep(1000);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("interrupted", error);
            }
        }
        state.record("ProfileCommand", message.commandId());
    }
}
