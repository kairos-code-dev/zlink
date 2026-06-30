package systems.zlink.e2e.registrationcodec.main.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class ManualSendHandler
    implements ZLinkSendHandler<Contracts.EchoManualCommand> {
    private final EvidenceStore state;

    public ManualSendHandler(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.EchoManualCommand message,
        ZLinkSendContext context) {
        state.record("Send", context.packetName().orElse("EchoManual"), message.value());
    }
}
