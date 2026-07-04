package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class DeliveryStatusChangedHandler
    implements ZLinkRequestHandler<Messages.DeliveryStatusChanged, Messages.DeliveryStatusAck> {
    private final EvidenceStore evidenceStore;
    private final ZLinkActorClient actors;

    public DeliveryStatusChangedHandler(
        EvidenceStore evidenceStore,
        ZLinkActorClient actors) {
        this.evidenceStore = evidenceStore;
        this.actors = actors;
    }

    @Override
    public Messages.DeliveryStatusAck handle(
        Messages.DeliveryStatusChanged request,
        ZLinkRequestContext context) {
        evidenceStore.append(request);
        actors.sendToActor(
                request.customerId(),
                new Messages.DeliveryStatusUpdatedMsg(
                    request.deliveryId(),
                    request.customerId(),
                    request.status(),
                    request.courierId(),
                    request.occurredAt()))
            .packetName("DeliveryStatusUpdatedMsg")
            .await();
        return new Messages.DeliveryStatusAck(request.deliveryId(), request.status());
    }
}
