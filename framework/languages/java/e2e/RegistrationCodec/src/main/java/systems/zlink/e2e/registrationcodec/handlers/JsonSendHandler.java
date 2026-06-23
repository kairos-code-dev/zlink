package systems.zlink.e2e.registrationcodec.handlers;

import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class JsonSendHandler
    implements ZLinkSendHandler<Contracts.JsonEchoCommand> {
    private final ScenarioState state;

    public JsonSendHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.JsonEchoCommand message,
        ZLinkSendContext context) {
        state.record("Send", "JsonEcho", message.value());
    }
}
