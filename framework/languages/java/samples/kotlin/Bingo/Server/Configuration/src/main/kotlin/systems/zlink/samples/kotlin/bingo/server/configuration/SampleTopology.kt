package systems.zlink.samples.kotlin.bingo.server.configuration

object SampleTopology {
    val RegistryPubEndpoint: String = property("registryPubEndpoint", "tcp://127.0.0.1:47101")
    val RegistryRouterEndpoint: String = property("registryRouterEndpoint", "tcp://127.0.0.1:47102")
    val ApiChannelEndpoint: String = property("apiChannelEndpoint", "tcp://127.0.0.1:47103")
    val ApiAChannelEndpoint: String = property("apiAChannelEndpoint", ApiChannelEndpoint)
    val ApiBChannelEndpoint: String = property("apiBChannelEndpoint", "tcp://127.0.0.1:47117")
    val PlayChannelEndpoint: String = property("playChannelEndpoint", "tcp://127.0.0.1:47104")
    val PlayAChannelEndpoint: String = property("playAChannelEndpoint", PlayChannelEndpoint)
    val PlayBChannelEndpoint: String = property("playBChannelEndpoint", "tcp://127.0.0.1:47118")
    val SessionSpotEndpoint: String = property("sessionSpotEndpoint", "tcp://127.0.0.1:47105")
    val SessionRouterEndpoint: String = property("sessionRouterEndpoint", "tcp://127.0.0.1:47106")
    val SessionASpotEndpoint: String = property("sessionASpotEndpoint", SessionSpotEndpoint)
    val SessionBSpotEndpoint: String = property("sessionBSpotEndpoint", "tcp://127.0.0.1:47119")
    val SessionARouterEndpoint: String = property("sessionARouterEndpoint", SessionRouterEndpoint)
    val SessionBRouterEndpoint: String = property("sessionBRouterEndpoint", "tcp://127.0.0.1:47120")
    val PlaySpotEndpoint: String = property("playSpotEndpoint", "tcp://127.0.0.1:47110")
    val PlaySpotRouterEndpoint: String = property("playSpotRouterEndpoint", "tcp://127.0.0.1:47111")
    val PlayASpotEndpoint: String = property("playASpotEndpoint", PlaySpotEndpoint)
    val PlayBSpotEndpoint: String = property("playBSpotEndpoint", "tcp://127.0.0.1:47121")
    val PlayASpotRouterEndpoint: String = property("playASpotRouterEndpoint", PlaySpotRouterEndpoint)
    val PlayBSpotRouterEndpoint: String = property("playBSpotRouterEndpoint", "tcp://127.0.0.1:47122")
    val SessionRouteEndpoint: String = property("sessionRouteEndpoint", "tcp://127.0.0.1:47112")
    val PlayRouteEndpoint: String = property("playRouteEndpoint", "tcp://127.0.0.1:47113")
    val SessionAPlayRouteEndpoint: String = property("sessionAPlayRouteEndpoint", SessionRouteEndpoint)
    val SessionBPlayRouteEndpoint: String = property("sessionBPlayRouteEndpoint", "tcp://127.0.0.1:47123")
    val PlayARouteEndpoint: String = property("playARouteEndpoint", PlayRouteEndpoint)
    val PlayBRouteEndpoint: String = property("playBRouteEndpoint", "tcp://127.0.0.1:47124")
    val StreamEndpoint: String = property("streamEndpoint", "tcp://127.0.0.1:47114")
    val SessionAStreamEndpoint: String = property("sessionAStreamEndpoint", StreamEndpoint)
    val SessionBStreamEndpoint: String = property("sessionBStreamEndpoint", "tcp://127.0.0.1:47125")
    val RedisEndpoint: String = requiredProperty("redisEndpoint")
    val RedisKeyPrefix: String = property("redisKeyPrefix", "bingo:kotlin:")
    val ApiNode: String = property("apiNode", "a")
    val PlayNode: String = property("playNode", "a")
    val SessionNode: String = property("sessionNode", "a")
    val SessionARouteRid: String = property("sessionAPlayRouteRid", "1201")
    val SessionBRouteRid: String = property("sessionBPlayRouteRid", "1202")
    val ApiARouteRid: String = property("apiAPlayRouteRid", "1301")
    val ApiBRouteRid: String = property("apiBPlayRouteRid", "1302")
    val SessionARouterRid: String = property("sessionARouterRid", "1101")
    val SessionBRouterRid: String = property("sessionBRouterRid", "1102")
    val PlayANodeRid: String = property("playANodeRid", "2201")
    val PlayBNodeRid: String = property("playBNodeRid", "2202")
    val PlayRid: String = property("playRid", PlayBNodeRid)

    fun selectedApiChannelEndpoint(): String =
        if (ApiNode == "b") ApiBChannelEndpoint else ApiAChannelEndpoint

    fun selectedApiRouteRid(): String =
        if (ApiNode == "b") ApiBRouteRid else ApiARouteRid

    fun selectedPlayChannelEndpoint(): String =
        if (PlayNode == "b") PlayBChannelEndpoint else PlayAChannelEndpoint

    fun selectedPlayRouteEndpoint(): String =
        if (PlayNode == "b") PlayBRouteEndpoint else PlayARouteEndpoint

    fun peerPlayRouteEndpoint(): String =
        if (PlayNode == "b") PlayARouteEndpoint else PlayBRouteEndpoint

    fun selectedPlaySpotEndpoint(): String =
        if (PlayNode == "b") PlayBSpotEndpoint else PlayASpotEndpoint

    fun selectedPlaySpotRouterEndpoint(): String =
        if (PlayNode == "b") PlayBSpotRouterEndpoint else PlayASpotRouterEndpoint

    fun selectedPlayNodeRid(): String =
        if (PlayNode == "b") PlayBNodeRid else PlayANodeRid

    fun preferredPlayNodeRid(): String =
        if (SessionNode == "b") PlayBNodeRid else PlayANodeRid

    fun selectedSessionSpotEndpoint(): String =
        if (SessionNode == "b") SessionBSpotEndpoint else SessionASpotEndpoint

    fun selectedSessionRouterEndpoint(): String =
        if (SessionNode == "b") SessionBRouterEndpoint else SessionARouterEndpoint

    fun selectedSessionRouterRid(): String =
        if (SessionNode == "b") SessionBRouterRid else SessionARouterRid

    fun selectedSessionRouteEndpoint(): String =
        if (SessionNode == "b") SessionBPlayRouteEndpoint else SessionAPlayRouteEndpoint

    fun selectedSessionRouteRid(): String =
        if (SessionNode == "b") SessionBRouteRid else SessionARouteRid

    fun selectedStreamEndpoint(): String =
        if (SessionNode == "b") SessionBStreamEndpoint else SessionAStreamEndpoint

    private fun property(name: String, defaultValue: String): String =
        System.getProperty("zlink.samples.bingo.$name", defaultValue)

    // The sample owns its Redis via run_sample.sh/run_sample.ps1, which provisions
    // an isolated container; require the endpoint so a stray direct run never
    // silently falls back to a developer's local Redis.
    private fun requiredProperty(name: String): String {
        val value = System.getProperty("zlink.samples.bingo.$name")
        require(!value.isNullOrBlank()) {
            "System property 'zlink.samples.bingo.$name' is required; run the sample via " +
                "run_sample.sh/run_sample.ps1, which provisions an isolated Redis container."
        }
        return value
    }
}
