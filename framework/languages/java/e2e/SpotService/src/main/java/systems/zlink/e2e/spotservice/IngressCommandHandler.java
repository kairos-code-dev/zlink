package systems.zlink.e2e.spotservice;

import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class IngressCommandHandler implements ZLinkSendHandler<Contracts.OutboundCommand> {
    private final ScenarioState state;

    public IngressCommandHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.OutboundCommand message,
        ZLinkSendContext context) {
        state.record("IngressCommand", "channel", message.value());
    }
}
