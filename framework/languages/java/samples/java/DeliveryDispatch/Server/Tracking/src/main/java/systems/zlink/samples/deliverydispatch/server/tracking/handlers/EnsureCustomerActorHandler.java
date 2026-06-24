package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
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
        var actor = ZLinkAwait.await(
            actors.getOrCreate(request.customerId(), SampleNames.CustomerActorType, request));
        return new Messages.CustomerActorEnsured(
            request.customerId(),
            new Messages.ActorRefSnapshot(
                actor.nodeRid().toBytes(),
                actor.actorId(),
                actor.epoch()));
    }
}
