package systems.zlink.e2e.registrationcodec.main.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.e2e.registrationcodec.main.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;

public final class JsonSendHandler
    implements ZLinkSendHandler<Contracts.JsonEchoCommand> {
    private final EvidenceStore state;

    public JsonSendHandler(EvidenceStore state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.JsonEchoCommand message,
        ZLinkSendContext context) {
        state.record("Send", "JsonEcho", message.value());
        state.record("ContentType", "JsonEcho", context.contentType().orElse("missing"));
    }
}
