package systems.zlink.samples.deliverydispatch.server.customergateway.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActorDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class SubscribeDeliverySessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.SubscribeDelivery> {
    private static final String CustomerId = "customer-1";

    private final ZLinkClient channels;
    private final CustomerActorDirectory customers;

    public SubscribeDeliverySessionHandler(
        ZLinkClient channels,
        CustomerActorDirectory customers) {
        this.channels = channels;
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
        Messages.CustomerActorEnsured ensured = channels
            .requestToChannel(
                SampleNames.CustomerRouteChannel,
                new Messages.EnsureCustomerActor(CustomerId))
            .await(Messages.CustomerActorEnsured.class);
        if (context.actors().find(ensured.actorRef().actorId()).isEmpty()) {
            await(context.actors().bind(new ZLinkActorRef(
                RoutingId.from(ensured.actorRef().nodeRid()),
                ensured.actorRef().actorId(),
                ensured.actorRef().generation())));
        }
        customers.subscribe(ensured.customerId(), request.deliveryId());
        context.client()
            .reply(new Messages.SubscribeDeliveryAccepted(request.deliveryId()))
            .submit();
    }
}
