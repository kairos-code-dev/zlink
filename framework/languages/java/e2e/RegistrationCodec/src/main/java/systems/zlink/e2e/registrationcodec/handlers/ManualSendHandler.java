package systems.zlink.e2e.registrationcodec.handlers;

import systems.zlink.e2e.registrationcodec.Contracts;
import systems.zlink.e2e.registrationcodec.ScenarioState;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class ManualSendHandler
    implements ZLinkSendHandler<Contracts.EchoManualCommand> {
    private final ScenarioState state;

    public ManualSendHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.EchoManualCommand message,
        ZLinkSendContext context) {
        state.record("Send", context.packetName().orElse("EchoManual"), message.value());
    }
}
