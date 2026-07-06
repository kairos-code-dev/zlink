package systems.zlink.samples.deliverydispatch.server.customergateway.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActorDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class SubscribeDeliverySessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.SubscribeDelivery> {
    private static final String CustomerId = "customer-1";

    private final ZLinkActorManager actors;
    private final CustomerActorDirectory customers;

    public SubscribeDeliverySessionHandler(
        ZLinkActorManager actors,
        CustomerActorDirectory customers) {
        this.actors = actors;
        this.customers = customers;
    }

    @Override
    public String packetName() {
        return "SubscribeDelivery";
    }

    @Override
    public Class<Messages.SubscribeDelivery> messageType() {
        return Messages.SubscribeDelivery.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Messages.SubscribeDelivery request) {
        ZLinkActorRef actor = await(actors.getOrCreate(
            CustomerId,
            SampleNames.CustomerActorType,
            new Messages.EnsureCustomerActor(CustomerId)));
        if (context.actors().find(actor.actorId()).isEmpty()) {
            await(context.actors().bind(actor));
        }
        customers.subscribe(CustomerId, request.deliveryId());
        context.client()
            .reply(new Messages.SubscribeDeliveryAccepted(request.deliveryId()))
            .submit();
    }
}
