package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration

object SampleTopology {
    const val RegistryPubEndpoint: String = "tcp://127.0.0.1:19191"
    const val RegistryRouterEndpoint: String = "tcp://127.0.0.1:19192"
    const val PlayRouteEndpoint: String = "inproc://zlink-kotlin-sample-session-play-route"
    const val ApiEndpoint: String = "tcp://127.0.0.1:47503"
    const val PlayEndpoint: String = "tcp://127.0.0.1:47504"
    const val SessionEndpoint: String = "tcp://127.0.0.1:47512"
}
