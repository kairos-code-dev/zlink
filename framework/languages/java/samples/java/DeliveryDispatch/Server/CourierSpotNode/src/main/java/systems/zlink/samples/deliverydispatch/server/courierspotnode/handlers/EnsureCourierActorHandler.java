package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class EnsureCourierActorHandler
    implements ZLinkSpotRequestHandler<CourierEntrySpot, Messages.EnsureCourierActor, Messages.CourierActorEnsured> {
    private final ZLinkActorManager actors;

    public EnsureCourierActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.CourierActorEnsured handle(
        CourierEntrySpot spot,
        Messages.EnsureCourierActor request) {
        ActorRef actor = await(actors.getOrCreate(
            request.courierId(),
            SampleNames.CourierActorType,
            request));
        return new Messages.CourierActorEnsured(
            request.courierId(),
            Messages.ActorRefWire.from(actor));
    }
}
