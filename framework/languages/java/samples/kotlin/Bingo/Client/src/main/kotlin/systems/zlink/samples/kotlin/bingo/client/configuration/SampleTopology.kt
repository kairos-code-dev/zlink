package systems.zlink.samples.kotlin.bingo.client.configuration

object SampleTopology {
    val StreamEndpoint: String = property("streamEndpoint", "tcp://127.0.0.1:47114")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.bingo.$name", defaultValue)
}
