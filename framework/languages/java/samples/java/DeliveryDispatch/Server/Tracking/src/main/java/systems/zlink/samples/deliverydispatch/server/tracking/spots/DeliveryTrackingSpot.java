package systems.zlink.samples.deliverydispatch.server.tracking.spots;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class DeliveryTrackingSpot implements ZLinkSpot {
    private final ZLinkSpotContext context;
    private final DeliverySpotDirectory directory;
    private final ObjectMapper json;
    private final Map<String, ZLinkActor> customers = new LinkedHashMap<>();
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
    public ZLinkSpotCreateResponse onCreate(Message request) {
        Messages.DeliverySpotCreate create = decode(request, Messages.DeliverySpotCreate.class);
        this.deliveryId = create.deliveryId();
        directory.add(deliveryId, this);
        return ZLinkSpotCreateResponse.accept(encode(new Messages.DeliverySpotCreated(deliveryId)));
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        ZLinkActor actor,
        Message request,
        CancellationToken cancellationToken) {
        Messages.DeliverySpotJoin join = decode(request, Messages.DeliverySpotJoin.class);
        if (!join.deliveryId().equals(deliveryId)) {
            return ZLinkSpotActorJoinResponse.reject();
        }
        customers.put(actor.actorId(), actor);
        System.err.printf(
            "deliverydispatch tracking spot: joined delivery=%s customer=%s%n",
            join.deliveryId(),
            actor.actorId());
        return ZLinkSpotActorJoinResponse.accept(
            encode(new Messages.DeliverySpotJoined(join.deliveryId(), actor.actorId())));
    }

    @Override
    public void onLeaveActor(ZLinkActor actor, CancellationToken cancellationToken) {
        customers.remove(actor.actorId());
    }

    public void record(Messages.DeliveryStatusChanged status) {
        history.add(status);
    }

    private <T> T decode(Message request, Class<T> type) {
        try {
            return json.readValue(request.toByteArray(), type);
        } catch (IOException ex) {
            throw new IllegalArgumentException("Failed to decode " + type.getSimpleName() + ".", ex);
        }
    }

    private Message encode(Object reply) {
        try {
            return Message.from(json.writeValueAsBytes(reply));
        } catch (JsonProcessingException ex) {
            throw new IllegalArgumentException("Failed to encode delivery spot reply.", ex);
        }
    }
}
