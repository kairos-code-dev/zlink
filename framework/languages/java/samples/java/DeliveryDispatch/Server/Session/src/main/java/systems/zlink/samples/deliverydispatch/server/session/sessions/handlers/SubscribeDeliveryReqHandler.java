package systems.zlink.samples.deliverydispatch.server.session.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.session.sessions.CustomerSessionDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class SubscribeDeliveryReqHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.SubscribeDeliveryReq> {
    private static final String CustomerId = "customer-1";

    private final ZLinkClient channels;
    private final CustomerSessionDirectory sessions;

    public SubscribeDeliveryReqHandler(
        ZLinkClient channels,
        CustomerSessionDirectory sessions) {
        this.channels = channels;
        this.sessions = sessions;
    }

    @Override
    public String packetName() {
        return "SubscribeDeliveryReq";
    }

    @Override
    public Class<Messages.SubscribeDeliveryReq> messageType() {
        return Messages.SubscribeDeliveryReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Messages.SubscribeDeliveryReq request) {
        if (request.deliveryId() == null || request.deliveryId().isBlank()) {
            throw new IllegalArgumentException("SubscribeDeliveryReq requires deliveryId.");
        }

        Messages.EnsureCustomerActorRes ensured = channels
            .requestToChannel(SampleNames.TrackingChannel, new Messages.EnsureCustomerActorReq(CustomerId))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.EnsureCustomerActorRes.class);
        await(context.actors()
            .bind(new ZLinkActorRef(
                RoutingId.from(ensured.actor().nodeRid()),
                ensured.actor().actorId(),
                ensured.actor().generation())));
        System.err.printf(
            "deliverydispatch session: bound customer actor=%s%n",
            ensured.actor().actorId());

        Messages.SubscribeCustomerToDeliveryRes subscribed = channels
            .requestToChannel(
                SampleNames.TrackingChannel,
                new Messages.SubscribeCustomerToDeliveryReq(CustomerId, request.deliveryId()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.SubscribeCustomerToDeliveryRes.class);
        sessions.subscribe(context, subscribed.deliveryId());
        context.client()
            .reply(new Messages.SubscribeDeliveryReqRes(subscribed.deliveryId()))
            .await();
    }
}
