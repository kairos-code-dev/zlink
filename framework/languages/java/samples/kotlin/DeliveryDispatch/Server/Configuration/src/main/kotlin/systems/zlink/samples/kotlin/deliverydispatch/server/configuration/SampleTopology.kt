package systems.zlink.samples.kotlin.deliverydispatch.server.configuration

object SampleTopology {
    val RegistryPubEndpoint = property("registryPubEndpoint", "tcp://127.0.0.1:47390")
    val RegistryRouterEndpoint = property("registryRouterEndpoint", "tcp://127.0.0.1:47391")
    val ApiChannelEndpoint = property("apiChannelEndpoint", "tcp://127.0.0.1:47392")
    val DispatchChannelEndpoint = property("dispatchChannelEndpoint", "tcp://127.0.0.1:47394")
    val CourierAEndpoint = property("courierAEndpoint", "tcp://127.0.0.1:47395")
    val CourierBEndpoint = property("courierBEndpoint", "tcp://127.0.0.1:47396")
    val TrackingChannelEndpoint = property("trackingChannelEndpoint", "tcp://127.0.0.1:47397")
    val StatusFanoutEndpoint = property("statusFanoutEndpoint", "tcp://127.0.0.1:47411")
    val TrackingSpotRouterEndpoint = property("trackingSpotRouterEndpoint", "tcp://127.0.0.1:47398")
    val TrackingSpotEndpoint = property("trackingSpotEndpoint", "tcp://127.0.0.1:47399")
    val SessionStreamEndpoint = property("sessionStreamEndpoint", "tcp://127.0.0.1:47400")
    val SessionSpotRouterEndpoint = property("sessionSpotRouterEndpoint", "tcp://127.0.0.1:47401")
    val SessionSpotEndpoint = property("sessionSpotEndpoint", "tcp://127.0.0.1:47402")
    val TrackingSpotRouteEndpoint = property("trackingSpotRouteEndpoint", "tcp://127.0.0.1:47403")
    val SessionSpotRouteEndpoint = property("sessionSpotRouteEndpoint", "tcp://127.0.0.1:47404")

    const val TrackingSpotNodeRid = "7050"
    const val SessionSpotNodeRid = "7051"
    const val SessionSpotPubRid = "7052"

    fun courierEndpoint(courierId: String): String =
        when (courierId) {
            SampleNames.CourierA -> CourierAEndpoint
            SampleNames.CourierB -> CourierBEndpoint
            else -> throw IllegalArgumentException("Unknown courier '$courierId'.")
        }

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.deliverydispatch.$name", defaultValue)
}
