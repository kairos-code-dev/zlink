package systems.zlink.samples.kotlin.bingo.server.configuration

import org.springframework.boot.context.properties.ConfigurationProperties

@ConfigurationProperties("sample")
data class SampleTopology(
    val apiAChannelEndpoint: String = "",
    val apiBChannelEndpoint: String = "",
    val playAChannelEndpoint: String = "",
    val playBChannelEndpoint: String = "",
    val sessionASpotEndpoint: String = "",
    val sessionBSpotEndpoint: String = "",
    val sessionARouterEndpoint: String = "",
    val sessionBRouterEndpoint: String = "",
    val playASpotEndpoint: String = "",
    val playBSpotEndpoint: String = "",
    val playASpotRouterEndpoint: String = "",
    val playBSpotRouterEndpoint: String = "",
    val sessionAStreamEndpoint: String = "",
    val sessionBStreamEndpoint: String = "",
    val redisEndpoint: String = "",
    val redisKeyPrefix: String = "bingo:kotlin:",
    val apiNode: String = "a",
    val playNode: String = "a",
    val sessionNode: String = "a",
    val sessionARouterRid: String = "1101",
    val sessionBRouterRid: String = "1102",
    val playANodeRid: String = "2201",
    val playBNodeRid: String = "2202",
    val playRid: String = "",
    val logDirectory: String = "",
) {
    init {
        listOf(
            "apiAChannelEndpoint" to apiAChannelEndpoint,
            "apiBChannelEndpoint" to apiBChannelEndpoint,
            "playAChannelEndpoint" to playAChannelEndpoint,
            "playBChannelEndpoint" to playBChannelEndpoint,
            "sessionASpotEndpoint" to sessionASpotEndpoint,
            "sessionBSpotEndpoint" to sessionBSpotEndpoint,
            "sessionARouterEndpoint" to sessionARouterEndpoint,
            "sessionBRouterEndpoint" to sessionBRouterEndpoint,
            "playASpotEndpoint" to playASpotEndpoint,
            "playBSpotEndpoint" to playBSpotEndpoint,
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
    fun selectedPlayChannelEndpoint() = if (playNode == "b") playBChannelEndpoint else playAChannelEndpoint
    fun selectedPlaySpotEndpoint() = if (playNode == "b") playBSpotEndpoint else playASpotEndpoint
    fun peerPlaySpotEndpoint() = if (playNode == "b") playASpotEndpoint else playBSpotEndpoint
    fun selectedPlaySpotRouterEndpoint() = if (playNode == "b") playBSpotRouterEndpoint else playASpotRouterEndpoint
    fun preferredPlaySpotRouterEndpoint() = if (sessionNode == "b") playBSpotRouterEndpoint else playASpotRouterEndpoint
    fun selectedPlayNodeRid() = if (playNode == "b") playBNodeRid else playANodeRid
    fun preferredPlayNodeRid() = if (sessionNode == "b") playBNodeRid else playANodeRid
    fun selectedSessionSpotEndpoint() = if (sessionNode == "b") sessionBSpotEndpoint else sessionASpotEndpoint
    fun selectedSessionRouterEndpoint() = if (sessionNode == "b") sessionBRouterEndpoint else sessionARouterEndpoint
    fun selectedSessionRouterRid() = if (sessionNode == "b") sessionBRouterRid else sessionARouterRid
    fun selectedStreamEndpoint() = if (sessionNode == "b") sessionBStreamEndpoint else sessionAStreamEndpoint
    fun effectivePlayRid() = playRid.ifBlank { playBNodeRid }

    companion object {
        fun configPath(args: Array<String>): String {
            require(args.size == 2 && args[0] == "--config" && args[1].isNotBlank()) {
                "Usage: <role executable> --config <path>"
            }
            return args[1]
        }
    }
}
