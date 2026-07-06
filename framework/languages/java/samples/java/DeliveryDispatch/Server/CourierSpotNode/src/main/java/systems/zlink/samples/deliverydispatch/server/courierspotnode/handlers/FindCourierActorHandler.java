package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRefSnapshot;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("courier-actor-node")
public final class FindCourierActorHandler
    implements ZLinkRequestHandler<Messages.FindCourierActor, Messages.CourierActorFound> {
    private final ZLinkActorManager actors;

    public FindCourierActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.CourierActorFound handle(
        Messages.FindCourierActor request,
        ZLinkRequestContext context) {
        return await(actors.find(request.courierId()))
            .map(actor -> new Messages.CourierActorFound(request.courierId(), ZLinkActorRefSnapshot.from(actor)))
            .orElseGet(() -> new Messages.CourierActorFound(request.courierId(), null));
    }
}
