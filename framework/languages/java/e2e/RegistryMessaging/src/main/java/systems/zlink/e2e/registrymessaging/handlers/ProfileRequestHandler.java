package systems.zlink.e2e.registrymessaging.handlers;

import systems.zlink.e2e.registrymessaging.ScenarioState;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class ProfileRequestHandler
    implements ZLinkRequestHandler<Contracts.ProfileRequest, Contracts.ProfileReply> {
    private final ScenarioState state;

    public ProfileRequestHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public Contracts.ProfileReply handle(
        Contracts.ProfileRequest request,
        ZLinkRequestContext context) {
        if (request.value().startsWith("slow")) {
            try {
                Thread.sleep(1000);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("interrupted", error);
            }
        }
        state.record("ProfileRequest", request.value());
        return new Contracts.ProfileReply(
            "profile:" + request.value(),
            state.providerRid(),
            state.instanceId());
    }
}
