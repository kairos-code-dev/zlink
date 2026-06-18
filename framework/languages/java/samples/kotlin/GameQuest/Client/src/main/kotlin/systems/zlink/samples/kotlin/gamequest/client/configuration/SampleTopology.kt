package systems.zlink.samples.kotlin.gamequest.client.configuration

object SampleTopology {
    val RegistryRouterEndpoint = property("registryRouterEndpoint", "tcp://127.0.0.1:47591")
    val GameApiAActionEndpoint = property("gameApiAActionEndpoint", "tcp://127.0.0.1:47594")
    val GameApiBActionEndpoint = property("gameApiBActionEndpoint", "tcp://127.0.0.1:47595")
    val GameApiAStreamEndpoint = property("gameApiAStreamEndpoint", "tcp://127.0.0.1:47596")
    val GameApiBStreamEndpoint = property("gameApiBStreamEndpoint", "tcp://127.0.0.1:47597")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.gamequest.$name", defaultValue)
}
