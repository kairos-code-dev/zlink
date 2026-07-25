package systems.zlink.samples.deliverydispatch.server.customergateway.sessions.handlers;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionMessageContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActorDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public final class SubscribeDeliverySessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.SubscribeDeliveryReq> {
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
    public Class<Messages.SubscribeDeliveryReq> messageType() {
        return Messages.SubscribeDeliveryReq.class;
    }

    @Override
    public CompletionStage<Void> handle(
        ZLinkSessionContext context,
        ZLinkSessionMessageContext dispatch,
        Messages.SubscribeDeliveryReq request) {
        Messages.FindCustomerActorReq find = new Messages.FindCustomerActorReq(CustomerId);
        return actors.find(find.customerId())
            .thenCompose(found -> found.isPresent()
                ? CompletableFuture.completedFuture(found.orElseThrow())
                : actors.getOrCreate(
                    CustomerId,
                    SampleNames.CustomerActorType,
                    new Messages.EnsureCustomerActorReq(CustomerId)))
            .thenCompose(actor -> context.actors().find(actor.actorId()).isEmpty()
                ? context.actors().bind(actor).thenApply(ignored -> actor)
                : CompletableFuture.completedFuture(actor))
            .thenAccept(actor -> {
                customers.subscribe(CustomerId, request.deliveryId());
                context.client().reply(
                    new Messages.SubscribeDeliveryRes(request.deliveryId())).submit();
            });
    }
}
