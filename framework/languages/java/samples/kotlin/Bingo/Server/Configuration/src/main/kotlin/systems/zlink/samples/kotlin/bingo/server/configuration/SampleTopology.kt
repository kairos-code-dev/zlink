package systems.zlink.samples.kotlin.bingo.server.configuration

object SampleTopology {
    val RegistryPubEndpoint: String = property("registryPubEndpoint", "tcp://127.0.0.1:47101")
    val RegistryRouterEndpoint: String = property("registryRouterEndpoint", "tcp://127.0.0.1:47102")
    val ApiChannelEndpoint: String = property("apiChannelEndpoint", "tcp://127.0.0.1:47103")
    val PlayChannelEndpoint: String = property("playChannelEndpoint", "tcp://127.0.0.1:47104")
    val SessionSpotEndpoint: String = property("sessionSpotEndpoint", "tcp://127.0.0.1:47105")
    val SessionRouterEndpoint: String = property("sessionRouterEndpoint", "tcp://127.0.0.1:47106")
    val PlaySpotEndpoint: String = property("playSpotEndpoint", "tcp://127.0.0.1:47110")
    val PlaySpotRouterEndpoint: String = property("playSpotRouterEndpoint", "tcp://127.0.0.1:47111")
    val SessionRouteEndpoint: String = property("sessionRouteEndpoint", "tcp://127.0.0.1:47112")
    val PlayRouteEndpoint: String = property("playRouteEndpoint", "tcp://127.0.0.1:47113")
    val StreamEndpoint: String = property("streamEndpoint", "tcp://127.0.0.1:47114")
    val RedisEndpoint: String = property("redisEndpoint", "127.0.0.1:6379")
    val RedisKeyPrefix: String = property("redisKeyPrefix", "bingo:kotlin:")
    const val SessionRouterRid: String = "1101"
    const val SessionPubRid: String = "1102"
    const val PlayRid: String = "2202"

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.bingo.$name", defaultValue)
}
