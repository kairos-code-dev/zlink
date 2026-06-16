package systems.zlink.samples.kotlin.deliverydispatch.probe.configuration

object SampleTopology {
    val RegistryPubEndpoint = property("registryPubEndpoint", "tcp://127.0.0.1:47390")
    val RegistryRouterEndpoint = property("registryRouterEndpoint", "tcp://127.0.0.1:47391")
    val TrackingChannelEndpoint = property("trackingChannelEndpoint", "tcp://127.0.0.1:47397")
    val StatusFanoutEndpoint = property("statusFanoutEndpoint", "tcp://127.0.0.1:47411")
    val TrackingSpotRouterEndpoint = property("trackingSpotRouterEndpoint", "tcp://127.0.0.1:47398")
    val SessionStreamEndpoint = property("sessionStreamEndpoint", "tcp://127.0.0.1:47400")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.deliverydispatch.$name", defaultValue)
}
