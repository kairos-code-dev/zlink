package systems.zlink.e2e.registrationcodec.main.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.AUTO_GROUP)
public final class AutoRequestHandler
    implements ZLinkRequestHandler<Contracts.EchoAutoRequest, Contracts.EchoReply> {
    private final EvidenceStore state;

    public AutoRequestHandler(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public Contracts.EchoReply handle(
        Contracts.EchoAutoRequest request,
        ZLinkRequestContext context) {
        state.record("Request", "EchoAuto", request.value());
        return new Contracts.EchoReply("echo:" + request.value(), "auto");
    }
}
