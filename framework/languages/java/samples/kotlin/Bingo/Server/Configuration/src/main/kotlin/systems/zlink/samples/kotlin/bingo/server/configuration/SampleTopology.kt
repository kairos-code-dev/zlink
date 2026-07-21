package systems.zlink.samples.kotlin.bingo.server.configuration

import org.springframework.boot.context.properties.ConfigurationProperties

@ConfigurationProperties("sample")
data class SampleTopology(
    val apiAChannelEndpoint: String = "",
    val apiBChannelEndpoint: String = "",
    val sessionARouterEndpoint: String = "",
    val sessionBRouterEndpoint: String = "",
    val playASpotRouterEndpoint: String = "",
    val playBSpotRouterEndpoint: String = "",
    val sessionAStreamEndpoint: String = "",
    val sessionBStreamEndpoint: String = "",
    val redisEndpoint: String = "",
    val redisKeyPrefix: String = "bingo:kotlin:",
    val apiNode: String = "a",
    val playNode: String = "a",
    val sessionNode: String = "a",
    val logDirectory: String = "",
) {
    init {
        listOf(
            "apiAChannelEndpoint" to apiAChannelEndpoint,
            "apiBChannelEndpoint" to apiBChannelEndpoint,
            "sessionARouterEndpoint" to sessionARouterEndpoint,
            "sessionBRouterEndpoint" to sessionBRouterEndpoint,
            "playASpotRouterEndpoint" to playASpotRouterEndpoint,
            "playBSpotRouterEndpoint" to playBSpotRouterEndpoint,
            "sessionAStreamEndpoint" to sessionAStreamEndpoint,
            "sessionBStreamEndpoint" to sessionBStreamEndpoint,
            "redisEndpoint" to redisEndpoint,
            "redisKeyPrefix" to redisKeyPrefix,
            "logDirectory" to logDirectory,
        ).forEach { (name, value) -> require(value.isNotBlank()) { "sample.$name is required" } }
        require(apiNode in setOf("a", "b")) { "sample.apiNode must be a or b" }
        require(playNode in setOf("a", "b")) { "sample.playNode must be a or b" }
        require(sessionNode in setOf("a", "b")) { "sample.sessionNode must be a or b" }
    }

    fun selectedApiChannelEndpoint() = if (apiNode == "b") apiBChannelEndpoint else apiAChannelEndpoint
    fun selectedPlaySpotRouterEndpoint() = if (playNode == "b") playBSpotRouterEndpoint else playASpotRouterEndpoint
    fun selectedSessionRouterEndpoint() = if (sessionNode == "b") sessionBRouterEndpoint else sessionARouterEndpoint
    fun selectedStreamEndpoint() = if (sessionNode == "b") sessionBStreamEndpoint else sessionAStreamEndpoint

    companion object {
        fun configPath(args: Array<String>): String {
            require(args.size == 2 && args[0] == "--config" && args[1].isNotBlank()) {
                "Usage: <role executable> --config <path>"
            }
            return args[1]
        }
    }
}
