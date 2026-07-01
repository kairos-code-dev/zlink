package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class ServerAssertionHandler
    implements ZLinkRequestHandler<Messages.ServerAssertionRequest, Messages.ServerAssertionResponse> {
    private final EvidenceStore evidenceStore;

    public ServerAssertionHandler(EvidenceStore evidenceStore) {
        this.evidenceStore = evidenceStore;
    }

    @Override
    public Messages.ServerAssertionResponse handle(
        Messages.ServerAssertionRequest request,
        ZLinkRequestContext context) {
        return evidenceStore.assertSequences(
            request.successfulDeliveryId(),
            request.reassignedDeliveryId());
    }
}
