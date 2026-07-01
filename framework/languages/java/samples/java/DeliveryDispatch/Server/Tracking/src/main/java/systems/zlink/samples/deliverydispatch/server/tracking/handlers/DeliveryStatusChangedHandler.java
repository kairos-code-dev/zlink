package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class DeliveryStatusChangedHandler
    implements ZLinkRequestHandler<Messages.DeliveryStatusChanged, Messages.DeliveryStatusAck> {
    private final EvidenceStore evidenceStore;
    private final ZLinkClient channels;

    public DeliveryStatusChangedHandler(
        EvidenceStore evidenceStore,
        ZLinkClient channels) {
        this.evidenceStore = evidenceStore;
        this.channels = channels;
    }

    @Override
    public Messages.DeliveryStatusAck handle(
        Messages.DeliveryStatusChanged request,
        ZLinkRequestContext context) {
        evidenceStore.append(request);
        channels.requestToChannel(SampleNames.CustomerRouteChannel, request)
            .await(Messages.DeliveryStatusAck.class);
        return new Messages.DeliveryStatusAck(request.deliveryId(), request.status());
    }
}
