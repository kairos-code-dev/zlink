package systems.zlink.samples.kotlin.bingo.probe.configuration

object SampleTopology {
    val RegistryPubEndpoint: String = property("registryPubEndpoint", "tcp://127.0.0.1:47101")
    val RegistryRouterEndpoint: String = property("registryRouterEndpoint", "tcp://127.0.0.1:47102")
    val ApiChannelEndpoint: String = property("apiChannelEndpoint", "tcp://127.0.0.1:47103")
    val PlayChannelEndpoint: String = property("playChannelEndpoint", "tcp://127.0.0.1:47104")
    val SessionSpotEndpoint: String = property("sessionSpotEndpoint", "tcp://127.0.0.1:47105")
    val SessionRouterEndpoint: String = property("sessionRouterEndpoint", "tcp://127.0.0.1:47106")
    val PlaySpotEndpoint: String = property("playSpotEndpoint", "tcp://127.0.0.1:47110")
    val PlaySpotRouterEndpoint: String = property("playSpotRouterEndpoint", "tcp://127.0.0.1:47111")
    val StreamEndpoint: String = property("streamEndpoint", "tcp://127.0.0.1:47114")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.bingo.$name", defaultValue)
}
