package systems.zlink.e2e.discoveryregistryha.provider;

import systems.zlink.e2e.discoveryregistryha.shared.Contracts;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class ProfileReqHandler
    implements ZLinkRequestHandler<Contracts.ProfileReq, Contracts.ProfileRes> {
    private final ProviderEvidenceStore evidence;

    public ProfileReqHandler(ProviderEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public Contracts.ProfileRes handle(
        Contracts.ProfileReq request,
        ZLinkRequestContext context) {
        evidence.record(request.marker(), request.value());
        return new Contracts.ProfileRes(
            "profile:" + request.value(),
            evidence.rid(),
            request.marker());
    }
}
