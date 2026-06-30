package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;

public final class EvidenceRouteRequestHandler
    implements ZLinkRouteRequestHandler<Contracts.EvidenceRequest, Contracts.EvidenceReply> {
    private final PlayEvidenceStore evidence;

    public EvidenceRouteRequestHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public Contracts.EvidenceReply handle(
        Contracts.EvidenceRequest request,
        ZLinkRouteRequestContext context) {
        return evidence.evidence(request.requestId());
    }
}
