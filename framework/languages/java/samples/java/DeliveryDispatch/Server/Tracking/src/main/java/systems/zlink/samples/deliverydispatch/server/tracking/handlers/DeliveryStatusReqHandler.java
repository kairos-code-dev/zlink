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
import systems.zlink.samples.deliverydispatch.server.tracking.spots.deliverytrackingspot.DeliverySpotDirectory;
import systems.zlink.samples.deliverydispatch.server.tracking.spots.deliverytrackingspot.DeliveryTrackingSpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class DeliveryStatusReqHandler
    implements ZLinkRequestHandler<Messages.DeliveryStatusReq, Messages.DeliveryStatusRes> {
    private final ZLinkSpotManager spots;
    private final DeliverySpotDirectory directory;
    private final EvidenceStore evidence;
    private final ZLinkFanoutClient fanout;
    private final ObjectMapper json;

    public DeliveryStatusReqHandler(
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
    public Messages.DeliveryStatusRes handle(
        Messages.DeliveryStatusReq request,
        ZLinkRequestContext context) {
        ZLinkAwait.await(spots.getOrCreate(
            DeliveryTrackingSpot.class,
            RoutingId.from(request.deliveryId()),
            new Messages.DeliverySpotCreateReq(request.deliveryId())));
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
        return new Messages.DeliveryStatusRes(request.deliveryId(), request.status());
    }

}
