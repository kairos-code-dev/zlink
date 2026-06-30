package systems.zlink.e2e.discoveryregistryha.embedded;

import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class ProfileRequestHandler
    implements ZLinkRequestHandler<Contracts.ProfileRequest, Contracts.ProfileReply> {
    private final EmbeddedEvidenceStore evidence;

    public ProfileRequestHandler(EmbeddedEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public Contracts.ProfileReply handle(
        Contracts.ProfileRequest request,
        ZLinkRequestContext context) {
        evidence.record(request.marker(), request.value());
        return new Contracts.ProfileReply(
            "profile:" + request.value(),
            evidence.rid(),
            request.marker());
    }
}
