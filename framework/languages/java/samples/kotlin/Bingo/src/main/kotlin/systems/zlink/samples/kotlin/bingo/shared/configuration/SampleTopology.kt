package systems.zlink.samples.kotlin.bingo.shared.configuration

object SampleTopology {
    const val RegistryPubEndpoint: String = "tcp://127.0.0.1:47101"
    const val RegistryRouterEndpoint: String = "tcp://127.0.0.1:47102"
    const val ApiChannelEndpoint: String = "tcp://127.0.0.1:47103"
    const val PlayChannelEndpoint: String = "tcp://127.0.0.1:47104"
    const val SessionSpotEndpoint: String = "tcp://127.0.0.1:47105"
    const val SessionRouterEndpoint: String = "tcp://127.0.0.1:47106"
    const val PlaySpotEndpoint: String = "tcp://127.0.0.1:47110"
    const val PlaySpotRouterEndpoint: String = "tcp://127.0.0.1:47111"
    const val StreamEndpoint: String = "tcp://127.0.0.1:47112"
    const val SessionPort: Int = 29100
}
