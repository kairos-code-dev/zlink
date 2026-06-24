package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.actors.ZLinkActorGateway;
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
    private final ZLinkActorGateway actorGateway;

    public EnsureCustomerActorHandler(
        ZLinkActorManager actors,
        ZLinkActorGateway actorGateway) {
        this.actors = actors;
        this.actorGateway = actorGateway;
    }

    @Override
    public Messages.CustomerActorEnsured handle(
        Messages.EnsureCustomerActor request,
        ZLinkRequestContext context) {
        var actor = ZLinkAwait.await(
            actors.getOrCreate(request.customerId(), SampleNames.CustomerActorType, request));
        var joined = actorGateway
            .joinEntrySpot(actor, RoutingId.from(SampleTopology.TrackingSpotNodeRid))
            .timeout(SampleTimings.RequestTimeout)
            .await();
        return new Messages.CustomerActorEnsured(
            request.customerId(),
            new Messages.ActorRefSnapshot(
                joined.actor().nodeRid().toBytes(),
                joined.actor().actorId(),
                joined.actor().epoch()));
    }
}
