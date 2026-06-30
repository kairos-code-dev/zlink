package systems.zlink.e2e.registrationcodec.jsononlypeer.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.jsononlypeer.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class MsgpackRequestHandler
    implements ZLinkRequestHandler<Contracts.PackedEchoRequest, Contracts.PackedEchoReply> {
    private final EvidenceStore state;

    public MsgpackRequestHandler(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public Contracts.PackedEchoReply handle(
        Contracts.PackedEchoRequest request,
        ZLinkRequestContext context) {
        state.record("Request", "MsgpackEcho", request.value());
        state.record("ContentType", "MsgpackEcho", context.contentType().orElse(""));
        return new Contracts.PackedEchoReply("echo:" + request.value());
    }
}
