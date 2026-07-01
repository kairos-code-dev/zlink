package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("courier-actor-node")
public final class EnsureCourierActorRouteHandler
    implements ZLinkRouteRequestHandler<Messages.EnsureCourierActor, Messages.CourierActorEnsured> {
    private final ZLinkActorManager actors;

    public EnsureCourierActorRouteHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.CourierActorEnsured handle(
        Messages.EnsureCourierActor request,
        ZLinkRouteRequestContext context) {
        ZLinkActorRef actor = await(actors.getOrCreate(
            request.courierId(),
            SampleNames.CourierActorType,
            request));
        return new Messages.CourierActorEnsured(
            request.courierId(),
            new Messages.ActorRefSnapshot(
                actor.nodeRid().toString(),
                actor.actorId(),
                actor.epoch()));
    }
}
