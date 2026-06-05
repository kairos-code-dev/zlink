package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration

object SampleTopology {
    val RegistryPubEndpoint: String = property("registryPubEndpoint", "tcp://127.0.0.1:19191")
    val RegistryRouterEndpoint: String = property("registryRouterEndpoint", "tcp://127.0.0.1:19192")
    val PlayRouteEndpoint: String = property("playRouteEndpoint", "tcp://127.0.0.1:47520")
    val PlaySpotRouterEndpoint: String = property("playSpotRouterEndpoint", "tcp://127.0.0.1:47521")
    val SessionRouteEndpoint: String = property("sessionRouteEndpoint", "tcp://127.0.0.1:47522")
    val SessionRouterEndpoint: String = property("sessionRouterEndpoint", "tcp://127.0.0.1:47523")
    val ReconnectSessionRouteEndpoint: String =
        property("reconnectSessionRouteEndpoint", "tcp://127.0.0.1:47524")
    val ReconnectSessionRouterEndpoint: String =
        property("reconnectSessionRouterEndpoint", "tcp://127.0.0.1:47525")
    val ApiEndpoint: String = property("apiEndpoint", "tcp://127.0.0.1:47503")
    val PlayChannelEndpoint: String = property("playEndpoint", "tcp://127.0.0.1:47504")
    val SessionEndpoint: String = property("sessionEndpoint", "tcp://127.0.0.1:47512")
    val ReconnectSessionEndpoint: String = property("reconnectSessionEndpoint", "tcp://127.0.0.1:47513")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.tictactoe.sessiongateway.$name", defaultValue)

    fun primarySession(): SampleSessionNode =
        SampleSessionNode(
            routeEndpoint = SessionRouteEndpoint,
            routerEndpoint = SessionRouterEndpoint,
            streamEndpoint = SessionEndpoint,
            routingId = SampleNames.SessionRid,
        )

    fun reconnectSession(): SampleSessionNode =
        SampleSessionNode(
            routeEndpoint = ReconnectSessionRouteEndpoint,
            routerEndpoint = ReconnectSessionRouterEndpoint,
            streamEndpoint = ReconnectSessionEndpoint,
            routingId = SampleNames.ReconnectSessionRid,
        )
}

data class SampleSessionNode(
    val routeEndpoint: String,
    val routerEndpoint: String,
    val streamEndpoint: String,
    val routingId: String,
)
