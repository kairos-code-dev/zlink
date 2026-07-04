package systems.zlink.samples.deliverydispatch.server.customergateway.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.actors.ZLinkActorRefSnapshot;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("customer-route")
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
        ZLinkActorRef actor = await(actors.getOrCreate(
            request.customerId(),
            SampleNames.CustomerActorType,
            request));
        return new Messages.CustomerActorEnsured(
            request.customerId(),
            ZLinkActorRefSnapshot.from(actor));
    }
}
