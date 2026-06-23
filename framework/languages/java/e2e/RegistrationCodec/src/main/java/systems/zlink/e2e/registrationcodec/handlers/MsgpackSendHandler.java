package systems.zlink.e2e.registrationcodec.handlers;

import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class MsgpackSendHandler
    implements ZLinkSendHandler<Contracts.PackedEchoCommand> {
    private final ScenarioState state;

    public MsgpackSendHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.PackedEchoCommand message,
        ZLinkSendContext context) {
        state.record("Send", "MsgpackEcho", message.value());
    }
}
