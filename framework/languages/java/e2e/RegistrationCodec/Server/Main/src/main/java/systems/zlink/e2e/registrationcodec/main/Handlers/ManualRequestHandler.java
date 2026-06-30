package systems.zlink.e2e.registrationcodec.main.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class ManualRequestHandler
    implements ZLinkRequestHandler<Contracts.EchoManualRequest, Contracts.EchoReply> {
    private final EvidenceStore state;

    public ManualRequestHandler(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public Contracts.EchoReply handle(
        Contracts.EchoManualRequest request,
        ZLinkRequestContext context) {
        state.record("Request", context.packetName().orElse("EchoManual"), request.value());
        return new Contracts.EchoReply("echo:" + request.value(), "manual");
    }
}
