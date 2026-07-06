package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class FindCourierActorHandler
    implements ZLinkSpotRequestHandler<CourierEntrySpot, Messages.FindCourierActor, Messages.CourierActorFound> {
    private final ZLinkActorManager actors;

    public FindCourierActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.CourierActorFound handle(
        CourierEntrySpot spot,
        Messages.FindCourierActor request) {
        return await(actors.find(request.courierId()))
            .map(actor -> new Messages.CourierActorFound(request.courierId(), Messages.ActorRefWire.from(actor)))
            .orElseGet(() -> new Messages.CourierActorFound(request.courierId(), null));
    }
}
