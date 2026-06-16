package systems.zlink.samples.kotlin.supportchat.client.configuration

object SampleTopology {
    val StreamEndpoint: String = property("streamEndpoint", "tcp://127.0.0.1:47214")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.supportchat.$name", defaultValue)
}
