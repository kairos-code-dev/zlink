package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.actors.ZLinkActorRefSnapshot;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("courier-actor-node")
public final class EnsureCourierActorHandler
    implements ZLinkRequestHandler<Messages.EnsureCourierActor, Messages.CourierActorEnsured> {
    private final ZLinkActorManager actors;

    public EnsureCourierActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.CourierActorEnsured handle(
        Messages.EnsureCourierActor request,
        ZLinkRequestContext context) {
        ZLinkActorRef actor = await(actors.getOrCreate(
            request.courierId(),
            SampleNames.CourierActorType,
            request));
        return new Messages.CourierActorEnsured(
            request.courierId(),
            ZLinkActorRefSnapshot.from(actor));
    }
}
