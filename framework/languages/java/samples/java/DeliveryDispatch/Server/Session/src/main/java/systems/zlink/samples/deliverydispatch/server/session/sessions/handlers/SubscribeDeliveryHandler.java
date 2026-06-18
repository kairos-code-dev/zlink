package systems.zlink.samples.deliverydispatch.server.session.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.session.sessions.CustomerSessionDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class SubscribeDeliveryHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.SubscribeDelivery> {
    private static final String CustomerId = "customer-1";

    private final ZLinkClient channels;
    private final CustomerSessionDirectory sessions;

    public SubscribeDeliveryHandler(
        ZLinkClient channels,
        CustomerSessionDirectory sessions) {
        this.channels = channels;
        this.sessions = sessions;
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
        ZLinkStreamHeader header,
        Messages.SubscribeDelivery request) {
        if (request.deliveryId() == null || request.deliveryId().isBlank()) {
            throw new IllegalArgumentException("SubscribeDelivery requires deliveryId.");
        }

        Messages.CustomerActorEnsured ensured = channels
            .requestToChannel(SampleNames.TrackingChannel, new Messages.EnsureCustomerActor(CustomerId))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.CustomerActorEnsured.class);
        await(context.actors()
            .bind(new ZLinkActorRef(
                RoutingId.from(ensured.actor().nodeRid()),
                ensured.actor().actorId(),
                ensured.actor().generation())));
        System.err.printf(
            "deliverydispatch session: bound customer actor=%s%n",
            ensured.actor().actorId());

        Messages.CustomerDeliverySubscribed subscribed = channels
            .requestToChannel(
                SampleNames.TrackingChannel,
                new Messages.SubscribeCustomerToDelivery(CustomerId, request.deliveryId()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.CustomerDeliverySubscribed.class);
        sessions.subscribe(context, subscribed.deliveryId());
        context.client()
            .reply(new Messages.SubscribeDeliveryAccepted(subscribed.deliveryId()))
            .await();
    }
}
