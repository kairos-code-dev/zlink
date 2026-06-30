package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class EnsureCustomerActorReqHandler
    implements ZLinkRequestHandler<Messages.EnsureCustomerActorReq, Messages.EnsureCustomerActorRes> {
    private final ZLinkActorManager actors;

    public EnsureCustomerActorReqHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.EnsureCustomerActorRes handle(
        Messages.EnsureCustomerActorReq request,
        ZLinkRequestContext context) {
        var actor = ZLinkAwait.await(
            actors.getOrCreate(request.customerId(), SampleNames.CustomerActorType, request));
        return new Messages.EnsureCustomerActorRes(
            request.customerId(),
            new Messages.ActorRefSnapshot(
                actor.nodeRid().toBytes(),
                actor.actorId(),
                actor.epoch()));
    }
}
