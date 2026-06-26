package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.deliverytrackingspot

import java.util.concurrent.ConcurrentHashMap

class DeliverySpotDirectory {
    private val spots = ConcurrentHashMap<String, DeliveryTrackingSpot>()

    fun add(deliveryId: String, spot: DeliveryTrackingSpot) {
        spots[deliveryId] = spot
    }

    fun require(deliveryId: String): DeliveryTrackingSpot =
        spots[deliveryId]
            ?: throw IllegalStateException("Delivery spot '$deliveryId' was not created.")
}
