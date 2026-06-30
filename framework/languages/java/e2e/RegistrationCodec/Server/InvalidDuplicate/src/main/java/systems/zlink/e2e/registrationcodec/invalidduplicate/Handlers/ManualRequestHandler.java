package systems.zlink.e2e.registrationcodec.invalidduplicate.Handlers;

import systems.zlink.e2e.registrationcodec.shared.Contracts;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class ManualRequestHandler
    implements ZLinkRequestHandler<Contracts.EchoManualRequest, Contracts.EchoReply> {
    @Override
    public Contracts.EchoReply handle(
        Contracts.EchoManualRequest request,
        ZLinkRequestContext context) {
        return new Contracts.EchoReply("echo:" + request.value(), "manual");
    }
}
