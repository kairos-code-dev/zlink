package systems.zlink.samples.kotlin.deliverydispatch.server.configuration

object SampleNames {
    const val DispatchChannel = "deliverydispatch.dispatch"
    const val CourierAChannel = "deliverydispatch.courier.a"
    const val CourierBChannel = "deliverydispatch.courier.b"
    const val TrackingChannel = "deliverydispatch.tracking"
    const val StatusFanoutChannel = "deliverydispatch.status"
    const val SpotRouteChannel = "deliverydispatch.spot-route"
    const val DeliverySpotDiscovery = "delivery-spots"
    const val TrackingSpotNode = "delivery-tracking-node"
    const val SessionSpotNode = "delivery-session-node"
    const val CustomerStreamNode = "delivery-customer-stream"
    const val CustomerActorType = "delivery-customer"

    const val CourierA = "courier-a"
    const val CourierB = "courier-b"

    fun courierChannel(courierId: String): String =
        when (courierId) {
            CourierA -> CourierAChannel
            CourierB -> CourierBChannel
            else -> throw IllegalArgumentException("Unknown courier '$courierId'.")
        }
}
