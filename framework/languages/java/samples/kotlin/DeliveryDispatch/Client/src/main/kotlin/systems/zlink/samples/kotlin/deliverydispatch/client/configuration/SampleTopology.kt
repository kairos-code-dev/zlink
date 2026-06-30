package systems.zlink.samples.kotlin.deliverydispatch.client.configuration

object SampleTopology {
    val RegistryRouterEndpoint = property("registryRouterEndpoint", "tcp://127.0.0.1:47391")
    val ApiHttpUrl = property("apiHttpUrl", "http://127.0.0.1:47392")
    val SessionStreamEndpoint = property("sessionStreamEndpoint", "tcp://127.0.0.1:47400")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.deliverydispatch.$name", defaultValue)
}
