package systems.zlink.samples.kotlin.gamequest.client.configuration

object SampleNames {
    const val ApiA = "api-a"
    const val ApiB = "api-b"

    fun gameApiActionChannel(apiName: String): String = "gamequest.gameapi.$apiName"
}
