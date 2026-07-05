package systems.zlink.samples.kotlin.shoppingmall.client.configuration

object SampleTopology {
    val CommerceApiAEndpoint = property("commerceApiAEndpoint", "tcp://127.0.0.1:47492")
    val CommerceApiBEndpoint = property("commerceApiBEndpoint", "tcp://127.0.0.1:47493")

    fun commerceApiEndpoint(instanceId: String): String =
        if (instanceId == SampleNames.ApiInstanceB) CommerceApiBEndpoint else CommerceApiAEndpoint

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.shoppingmall.$name", defaultValue)
}
