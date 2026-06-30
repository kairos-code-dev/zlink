package systems.zlink.e2e.registrationcodec.jsononlypeer.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.jsononlypeer.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class JsonRequestHandler
    implements ZLinkRequestHandler<Contracts.JsonEchoRequest, Contracts.EchoReply> {
    private final EvidenceStore state;

    public JsonRequestHandler(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public Contracts.EchoReply handle(
        Contracts.JsonEchoRequest request,
        ZLinkRequestContext context) {
        state.record("Request", "JsonEcho", request.value());
        state.record("ContentType", "JsonEcho", context.contentType().orElse("missing"));
        return new Contracts.EchoReply("echo:" + request.value(), "json");
    }
}
