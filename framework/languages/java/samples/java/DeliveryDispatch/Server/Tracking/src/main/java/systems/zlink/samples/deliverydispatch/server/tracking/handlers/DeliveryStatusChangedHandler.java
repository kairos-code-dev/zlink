package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class DeliveryStatusChangedHandler
    implements ZLinkRequestHandler<Messages.DeliveryStatusChanged, Messages.DeliveryStatusAck> {
    private final EvidenceStore evidenceStore;
    private final ZLinkActorClient actors;
    private final ZLinkActorDirectory actorRefs;

    public DeliveryStatusChangedHandler(
        EvidenceStore evidenceStore,
        ZLinkActorClient actors,
        ZLinkActorDirectory actorRefs) {
        this.evidenceStore = evidenceStore;
        this.actors = actors;
        this.actorRefs = actorRefs;
    }

    @Override
    public Messages.DeliveryStatusAck handle(
        Messages.DeliveryStatusChanged request,
        ZLinkRequestContext context) {
        evidenceStore.append(request);
        actors.sendToActor(
                actorRef(request.customerId()),
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

    private ActorRef actorRef(String actorId) {
        return ZLinkAwait.await(actorRefs.find(actorId))
            .orElseThrow(() -> new IllegalStateException("customer actor not found: " + actorId));
    }
}
