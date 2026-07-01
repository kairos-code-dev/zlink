package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.ActorDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("courier-actor-node")
public final class OfferDeliveryRouteHandler
    implements ZLinkRouteRequestHandler<Messages.OfferDelivery, Messages.OfferDeliveryResult> {
    private final ZLinkActorManager actors;
    private final ActorDirectory directory;

    public OfferDeliveryRouteHandler(
        ZLinkActorManager actors,
        ActorDirectory directory) {
        this.actors = actors;
        this.directory = directory;
    }

    @Override
    public Messages.OfferDeliveryResult handle(
        Messages.OfferDelivery request,
        ZLinkRouteRequestContext context) {
        ZLinkActorRef actor = await(actors.getOrCreate(
            request.courierId(),
            SampleNames.CourierActorType,
            request));
        return directory.require(actor.actorId()).offer(request);
    }
}
