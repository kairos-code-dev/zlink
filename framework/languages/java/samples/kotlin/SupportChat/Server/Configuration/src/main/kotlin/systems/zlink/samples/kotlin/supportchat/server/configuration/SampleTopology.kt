package systems.zlink.samples.kotlin.supportchat.server.configuration

object SampleTopology {
    val RegistryPubEndpoint: String = property("registryPubEndpoint", "tcp://127.0.0.1:47201")
    val RegistryRouterEndpoint: String = property("registryRouterEndpoint", "tcp://127.0.0.1:47202")
    val ApiChannelEndpoint: String = property("apiChannelEndpoint", "tcp://127.0.0.1:47203")
    val SupportChannelEndpoint: String = property("supportChannelEndpoint", "tcp://127.0.0.1:47204")
    val SessionSpotEndpoint: String = property("sessionSpotEndpoint", "tcp://127.0.0.1:47205")
    val SessionRouterEndpoint: String = property("sessionRouterEndpoint", "tcp://127.0.0.1:47206")
    val SupportSpotEndpoint: String = property("supportSpotEndpoint", "tcp://127.0.0.1:47210")
    val SupportSpotRouterEndpoint: String = property("supportSpotRouterEndpoint", "tcp://127.0.0.1:47211")
    val SessionRouteEndpoint: String = property("sessionRouteEndpoint", "tcp://127.0.0.1:47212")
    val SupportRouteEndpoint: String = property("supportRouteEndpoint", "tcp://127.0.0.1:47213")
    val StreamEndpoint: String = property("streamEndpoint", "tcp://127.0.0.1:47214")
    const val SessionRouterRid: String = "3101"
    const val SessionPubRid: String = "3102"
    const val SupportRid: String = "4202"

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.supportchat.$name", defaultValue)
}
