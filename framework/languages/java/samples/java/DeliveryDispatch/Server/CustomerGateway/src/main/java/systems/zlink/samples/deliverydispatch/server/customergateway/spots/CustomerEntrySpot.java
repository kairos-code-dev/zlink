package systems.zlink.samples.deliverydispatch.server.customergateway.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActor;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActorDirectory;
import systems.zlink.samples.deliverydispatch.server.customergateway.spots.handlers.SubscribeDeliveryActorHandler;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class CustomerEntrySpot implements ZLinkEntrySpot<CustomerActor> {
    private final ZLinkEntrySpotContext context;
    private final CustomerActorDirectory customers;

    public CustomerEntrySpot(
        ZLinkEntrySpotContext context,
        CustomerActorDirectory customers) {
        this.context = context;
        this.customers = customers;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addHandler(SubscribeDeliveryActorHandler.class);
    }

    @Override
    public void onCreateActor(
        CustomerActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken) {
        customers.register(actor);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        CustomerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }

    public Messages.SubscribeDeliveryAccepted subscribe(
        CustomerActor actor,
        Messages.SubscribeDelivery request) {
        customers.subscribe(actor.actorId(), request.deliveryId());
        return new Messages.SubscribeDeliveryAccepted(request.deliveryId());
    }
}
