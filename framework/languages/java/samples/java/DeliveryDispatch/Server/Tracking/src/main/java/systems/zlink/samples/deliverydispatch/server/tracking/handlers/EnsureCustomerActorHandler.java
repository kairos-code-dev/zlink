package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class EnsureCustomerActorHandler
    implements ZLinkRequestHandler<Messages.EnsureCustomerActor, Messages.CustomerActorEnsured> {
    private final ZLinkActorManager actors;

    public EnsureCustomerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.CustomerActorEnsured handle(
        Messages.EnsureCustomerActor request,
        ZLinkRequestContext context) {
        ZLinkActor actor = ZLinkAwait.await(
            actors.getOrCreate(request.customerId(), SampleNames.CustomerActorType));
        var joined = actor.context()
            .joinEntrySpot(
                RoutingId.from(SampleTopology.TrackingSpotNodeRid),
                Message.from(new byte[0]))
            .timeout(SampleTimings.RequestTimeout)
            .await(Message.class);
        return new Messages.CustomerActorEnsured(
            request.customerId(),
            new Messages.ActorRefSnapshot(
                joined.actor().nodeRid().toBytes(),
                joined.actor().actorId(),
                joined.actor().epoch()));
    }
}
