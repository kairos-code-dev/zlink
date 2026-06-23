package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.deliverydispatch.server.configuration.EvidenceStore;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.tracking.spots.DeliverySpotDirectory;
import systems.zlink.samples.deliverydispatch.server.tracking.spots.DeliveryTrackingSpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class DeliveryStatusChangedHandler
    implements ZLinkRequestHandler<Messages.DeliveryStatusChanged, Messages.DeliveryStatusAck> {
    private final ZLinkSpotManager spots;
    private final DeliverySpotDirectory directory;
    private final EvidenceStore evidence;
    private final ZLinkFanoutClient fanout;
    private final ObjectMapper json;

    public DeliveryStatusChangedHandler(
        ZLinkSpotManager spots,
        DeliverySpotDirectory directory,
        EvidenceStore evidence,
        ZLinkFanoutClient fanout,
        ObjectMapper json) {
        this.spots = spots;
        this.directory = directory;
        this.evidence = evidence;
        this.fanout = fanout;
        this.json = json;
    }

    @Override
    public Messages.DeliveryStatusAck handle(
        Messages.DeliveryStatusChanged request,
        ZLinkRequestContext context) {
        ZLinkAwait.await(spots.getOrCreate(
            DeliveryTrackingSpot.class,
            RoutingId.from(request.deliveryId()),
            new Messages.DeliverySpotCreate(request.deliveryId())));
        evidence.append(request);
        directory.require(request.deliveryId()).record(request);
        fanout.publish(
                SampleNames.StatusFanoutChannel,
                request.deliveryId(),
                new Messages.DeliveryStatusNotify(
                    request.deliveryId(),
                    request.status(),
                    request.courierId(),
                    request.occurredAtUnixMs()))
            .await();
        System.err.printf(
            "deliverydispatch tracking: status delivery=%s status=%s courier=%s%n",
            request.deliveryId(),
            request.status(),
            request.courierId());
        return new Messages.DeliveryStatusAck(request.deliveryId(), request.status());
    }

}
