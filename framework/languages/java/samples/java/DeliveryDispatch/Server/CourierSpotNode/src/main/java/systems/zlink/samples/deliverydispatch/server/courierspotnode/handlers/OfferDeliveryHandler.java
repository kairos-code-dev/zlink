package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.ActorDirectory;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletionStage;

public final class OfferDeliveryHandler
    implements ZLinkSpotRequestHandler<CourierEntrySpot, Messages.OfferDeliveryReq, Messages.OfferDeliveryRes> {
    private final ZLinkActorManager actors;
    private final ActorDirectory directory;

    public OfferDeliveryHandler(
        ZLinkActorManager actors,
        ActorDirectory directory) {
        this.actors = actors;
        this.directory = directory;
    }

    @Override
    public CompletionStage<Messages.OfferDeliveryRes> handle(
        CourierEntrySpot spot,
        Messages.OfferDeliveryReq request) {
        return actors.find(request.courierId()).thenApply(found -> {
            var actorRef = found.orElseThrow(() -> new IllegalStateException(
                "Courier actor is not bound: " + request.courierId()));
            return directory.require(actorRef.actorId()).offer(request);
        });
    }
}
