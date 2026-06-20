package systems.zlink.samples.kotlin.bingo.client.configuration

object SampleTopology {
    val StreamEndpoint: String = property("streamEndpoint", "tcp://127.0.0.1:47114")
    val SessionAStreamEndpoint: String = property("sessionAStreamEndpoint", StreamEndpoint)
    val SessionBStreamEndpoint: String = property("sessionBStreamEndpoint", "tcp://127.0.0.1:47125")

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.bingo.$name", defaultValue)
}
