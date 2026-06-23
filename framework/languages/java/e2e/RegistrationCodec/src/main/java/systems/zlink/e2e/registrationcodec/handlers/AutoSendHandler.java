package systems.zlink.e2e.registrationcodec.handlers;

import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.AUTO_GROUP)
public final class AutoSendHandler
    implements ZLinkSendHandler<Contracts.EchoAutoCommand> {
    private final ScenarioState state;

    public AutoSendHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.EchoAutoCommand message,
        ZLinkSendContext context) {
        state.record("Send", "EchoAuto", message.value());
    }
}
