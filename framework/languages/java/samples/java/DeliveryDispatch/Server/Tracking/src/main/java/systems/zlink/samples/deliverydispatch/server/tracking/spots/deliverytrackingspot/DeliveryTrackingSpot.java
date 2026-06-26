package systems.zlink.samples.deliverydispatch.server.tracking.spots.deliverytrackingspot;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.deliverydispatch.server.tracking.actors.CustomerActor;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class DeliveryTrackingSpot implements ZLinkSpot<CustomerActor> {
    private final ZLinkSpotContext context;
    private final DeliverySpotDirectory directory;
    private final ObjectMapper json;
    private final Map<String, CustomerActor> customers = new LinkedHashMap<>();
    private final List<Messages.DeliveryStatusChanged> history = new ArrayList<>();
    private String deliveryId = "";

    public DeliveryTrackingSpot(
        ZLinkSpotContext context,
        DeliverySpotDirectory directory,
        ObjectMapper json) {
        this.context = context;
        this.directory = directory;
        this.json = json;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        Messages.DeliverySpotCreate create = request.decode(Messages.DeliverySpotCreate.class);
        this.deliveryId = create.deliveryId();
        directory.add(deliveryId, this);
        return ZLinkSpotCreateResponse.accept(new Messages.DeliverySpotCreated(deliveryId));
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        CustomerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        Messages.DeliverySpotJoin join = request.decode(Messages.DeliverySpotJoin.class);
        if (!join.deliveryId().equals(deliveryId)) {
            return ZLinkSpotActorJoinResponse.reject();
        }
        customers.put(actor.actorId(), actor);
        System.err.printf(
            "deliverydispatch tracking spot: joined delivery=%s customer=%s%n",
            join.deliveryId(),
            actor.actorId());
        return ZLinkSpotActorJoinResponse.accept(
            new Messages.DeliverySpotJoined(join.deliveryId(), actor.actorId()));
    }

    @Override
    public void onLeaveActor(CustomerActor actor, CancellationToken cancellationToken) {
        customers.remove(actor.actorId());
    }

    public void record(Messages.DeliveryStatusChanged status) {
        history.add(status);
    }

}
