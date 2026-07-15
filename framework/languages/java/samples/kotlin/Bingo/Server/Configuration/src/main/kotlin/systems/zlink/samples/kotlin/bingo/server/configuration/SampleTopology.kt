package systems.zlink.samples.kotlin.bingo.server.configuration

import java.nio.file.Files
import java.nio.file.Path
import java.util.Properties

object SampleTopology {
    lateinit var ApiAChannelEndpoint: String
    lateinit var ApiBChannelEndpoint: String
    lateinit var PlayAChannelEndpoint: String
    lateinit var PlayBChannelEndpoint: String
    lateinit var SessionASpotEndpoint: String
    lateinit var SessionBSpotEndpoint: String
    lateinit var SessionARouterEndpoint: String
    lateinit var SessionBRouterEndpoint: String
    lateinit var PlayASpotEndpoint: String
    lateinit var PlayBSpotEndpoint: String
    lateinit var PlayASpotRouterEndpoint: String
    lateinit var PlayBSpotRouterEndpoint: String
    lateinit var SessionAStreamEndpoint: String
    lateinit var SessionBStreamEndpoint: String
    lateinit var RedisEndpoint: String
    lateinit var RedisKeyPrefix: String
    lateinit var ApiNode: String
    lateinit var PlayNode: String
    lateinit var SessionNode: String
    lateinit var SessionARouterRid: String
    lateinit var SessionBRouterRid: String
    lateinit var PlayANodeRid: String
    lateinit var PlayBNodeRid: String
    lateinit var PlayRid: String
    lateinit var LogDirectory: String

    fun configure(args: Array<String>) {
        val properties = load(args)
        ApiAChannelEndpoint = value(properties, "apiAChannelEndpoint", "tcp://127.0.0.1:47103")
        ApiBChannelEndpoint = value(properties, "apiBChannelEndpoint", "tcp://127.0.0.1:47117")
        PlayAChannelEndpoint = value(properties, "playAChannelEndpoint", "tcp://127.0.0.1:47104")
        PlayBChannelEndpoint = value(properties, "playBChannelEndpoint", "tcp://127.0.0.1:47118")
        SessionASpotEndpoint = value(properties, "sessionASpotEndpoint", "tcp://127.0.0.1:47105")
        SessionBSpotEndpoint = value(properties, "sessionBSpotEndpoint", "tcp://127.0.0.1:47119")
        SessionARouterEndpoint = value(properties, "sessionARouterEndpoint", "tcp://127.0.0.1:47106")
        SessionBRouterEndpoint = value(properties, "sessionBRouterEndpoint", "tcp://127.0.0.1:47120")
        PlayASpotEndpoint = value(properties, "playASpotEndpoint", "tcp://127.0.0.1:47110")
        PlayBSpotEndpoint = value(properties, "playBSpotEndpoint", "tcp://127.0.0.1:47121")
        PlayASpotRouterEndpoint = value(properties, "playASpotRouterEndpoint", "tcp://127.0.0.1:47111")
        PlayBSpotRouterEndpoint = value(properties, "playBSpotRouterEndpoint", "tcp://127.0.0.1:47122")
        SessionAStreamEndpoint = value(properties, "sessionAStreamEndpoint", "tcp://127.0.0.1:47114")
        SessionBStreamEndpoint = value(properties, "sessionBStreamEndpoint", "tcp://127.0.0.1:47125")
        RedisEndpoint = required(properties, "redisEndpoint")
        RedisKeyPrefix = value(properties, "redisKeyPrefix", "bingo:kotlin:")
        ApiNode = value(properties, "apiNode", "a")
        PlayNode = value(properties, "playNode", "a")
        SessionNode = value(properties, "sessionNode", "a")
        SessionARouterRid = value(properties, "sessionARouterRid", "1101")
        SessionBRouterRid = value(properties, "sessionBRouterRid", "1102")
        PlayANodeRid = value(properties, "playANodeRid", "2201")
        PlayBNodeRid = value(properties, "playBNodeRid", "2202")
        PlayRid = value(properties, "playRid", PlayBNodeRid)
        LogDirectory = required(properties, "logDirectory")
    }

    fun selectedApiChannelEndpoint(): String =
        if (ApiNode == "b") ApiBChannelEndpoint else ApiAChannelEndpoint

    fun selectedPlayChannelEndpoint(): String =
        if (PlayNode == "b") PlayBChannelEndpoint else PlayAChannelEndpoint

    fun selectedPlaySpotEndpoint(): String =
        if (PlayNode == "b") PlayBSpotEndpoint else PlayASpotEndpoint

    fun peerPlaySpotEndpoint(): String =
        if (PlayNode == "b") PlayASpotEndpoint else PlayBSpotEndpoint

    fun selectedPlaySpotRouterEndpoint(): String =
        if (PlayNode == "b") PlayBSpotRouterEndpoint else PlayASpotRouterEndpoint

    fun preferredPlaySpotRouterEndpoint(): String =
        if (SessionNode == "b") PlayBSpotRouterEndpoint else PlayASpotRouterEndpoint

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

    fun selectedStreamEndpoint(): String =
        if (SessionNode == "b") SessionBStreamEndpoint else SessionAStreamEndpoint

    private fun load(args: Array<String>): Properties {
        require(args.size == 2 && args[0] == "--config" && args[1].isNotBlank()) {
            "Usage: <role> --config <path>"
        }
        return Properties().also { properties ->
            Files.newBufferedReader(Path.of(args[1])).use(properties::load)
        }
    }

    private fun value(properties: Properties, name: String, fallback: String): String =
        properties.getProperty(name)?.takeIf(String::isNotBlank) ?: fallback

    private fun required(properties: Properties, name: String): String =
        requireNotNull(properties.getProperty(name)?.takeIf(String::isNotBlank)) {
            "Missing Bingo sample config: $name"
        }
}
