package systems.zlink.samples.deliverydispatch.server.tracking.spots;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class DeliverySpotDirectory {
    private final Map<String, DeliveryTrackingSpot> spots = new ConcurrentHashMap<>();

    public void add(String deliveryId, DeliveryTrackingSpot spot) {
        spots.put(deliveryId, spot);
    }

    public DeliveryTrackingSpot require(String deliveryId) {
        DeliveryTrackingSpot spot = spots.get(deliveryId);
        if (spot == null) {
            throw new IllegalStateException("Delivery spot '" + deliveryId + "' was not created.");
        }
        return spot;
    }
}
