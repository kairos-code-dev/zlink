package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.ActorDirectory;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class OfferDeliveryHandler
    implements ZLinkSpotRequestHandler<CourierEntrySpot, Messages.OfferDelivery, Messages.OfferDeliveryResult> {
    private final ZLinkActorManager actors;
    private final ActorDirectory directory;

    public OfferDeliveryHandler(
        ZLinkActorManager actors,
        ActorDirectory directory) {
        this.actors = actors;
        this.directory = directory;
    }

    @Override
    public Messages.OfferDeliveryResult handle(
        CourierEntrySpot spot,
        Messages.OfferDelivery request) {
        var actorRef = await(actors.find(request.courierId()))
            .orElseThrow(() -> new IllegalStateException("Courier actor is not bound: " + request.courierId()));
        return directory.require(actorRef.actorId()).offer(request);
    }
}
