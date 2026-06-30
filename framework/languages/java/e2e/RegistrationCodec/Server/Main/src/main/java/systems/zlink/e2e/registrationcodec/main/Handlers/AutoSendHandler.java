package systems.zlink.e2e.registrationcodec.main.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.AUTO_GROUP)
public final class AutoSendHandler
    implements ZLinkSendHandler<Contracts.EchoAutoCommand> {
    private final EvidenceStore state;

    public AutoSendHandler(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.EchoAutoCommand message,
        ZLinkSendContext context) {
        state.record("Send", "EchoAuto", message.value());
    }
}
