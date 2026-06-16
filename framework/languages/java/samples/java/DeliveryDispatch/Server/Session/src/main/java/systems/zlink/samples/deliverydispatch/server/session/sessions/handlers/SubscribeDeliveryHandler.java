package systems.zlink.samples.deliverydispatch.server.session.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.session.sessions.CustomerSessionDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

public final class SubscribeDeliveryHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
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
    public void handle(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        Messages.SubscribeDelivery request = ZLinkStreamJson.decode(
            new ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header)),
            Messages.SubscribeDelivery.class);
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

    private static ZLinkStreamCodec connectorCodec(ZLinkStreamHeader header) {
        return switch (header.codec()) {
            case RAW -> ZLinkStreamCodec.RAW;
            case JSON -> ZLinkStreamCodec.JSON;
            case MESSAGE_PACK -> ZLinkStreamCodec.MESSAGE_PACK;
            case PROTOBUF -> ZLinkStreamCodec.PROTOBUF;
        };
    }
}
