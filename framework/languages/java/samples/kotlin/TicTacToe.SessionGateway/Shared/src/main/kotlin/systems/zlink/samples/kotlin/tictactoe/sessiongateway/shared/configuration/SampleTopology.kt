package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration

object SampleTopology {
    val RegistryPubEndpoint: String = property("registryPubEndpoint", "tcp://127.0.0.1:19191")
    val RegistryRouterEndpoint: String = property("registryRouterEndpoint", "tcp://127.0.0.1:19192")
    val PlayRouteEndpoint: String = property("playRouteEndpoint", "tcp://127.0.0.1:47520")
    val ApiEndpoint: String = property("apiEndpoint", "tcp://127.0.0.1:47503")
    val PlayEndpoint: String = property("playEndpoint", "tcp://127.0.0.1:47504")
    val SessionEndpoint: String = property("sessionEndpoint", "tcp://127.0.0.1:47512")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.tictactoe.sessiongateway.$name", defaultValue)
}
