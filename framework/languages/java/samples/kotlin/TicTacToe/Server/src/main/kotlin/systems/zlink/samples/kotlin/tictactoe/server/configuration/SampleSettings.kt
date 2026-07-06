package systems.zlink.samples.kotlin.tictactoe.server.configuration

import java.net.URI
import java.nio.file.Files
import java.nio.file.Path
import java.util.Properties

data class SampleSettings(
    val apiBindUrl: String,
    val apiPublicUrl: String,
    val apiChannelEndpoint: String,
    val playChannelEndpoint: String,
    val playChannelEndpoints: List<String>,
    val playEndpoint: String,
    val playEndpoints: List<String>,
    val spotEndpoint: String,
    val spotEndpoints: List<String>,
    val spotPubSubEndpoint: String,
    val spotPubSubEndpoints: List<String>,
    val redisEndpoint: String,
    val redisKeyPrefix: String,
    val playSpotNodeRid: String,
    val peerPlaySpotNodeRid: String,
    val peerSpotEndpoint: String,
    val peerSpotPubSubEndpoint: String,
    val logDirectory: String,
) {
    val apiHttpPort: Int
        get() = URI.create(apiBindUrl).port

    val playIndex: Int
        get() = playChannelEndpoints.indexOf(playChannelEndpoint).takeIf { it >= 0 } ?: 0

    companion object {
        fun createDefault(): SampleSettings =
            SampleSettings(
                apiBindUrl = "http://127.0.0.1:18081",
                apiPublicUrl = "http://127.0.0.1:18081",
                apiChannelEndpoint = "tcp://127.0.0.1:47301",
                playChannelEndpoint = "tcp://127.0.0.1:47303",
                playChannelEndpoints = listOf("tcp://127.0.0.1:47303", "tcp://127.0.0.1:47313"),
                playEndpoint = "tcp://127.0.0.1:47302",
                playEndpoints = listOf("tcp://127.0.0.1:47302", "tcp://127.0.0.1:47312"),
                spotEndpoint = "tcp://127.0.0.1:47305",
                spotEndpoints = listOf("tcp://127.0.0.1:47305", "tcp://127.0.0.1:47315"),
                spotPubSubEndpoint = "tcp://127.0.0.1:47307",
                spotPubSubEndpoints = listOf("tcp://127.0.0.1:47307", "tcp://127.0.0.1:47317"),
                redisEndpoint = "",
                redisKeyPrefix = "zlink:tictactoe-kotlin:room:",
                playSpotNodeRid = "play-node-1",
                peerPlaySpotNodeRid = "play-node-2",
                peerSpotEndpoint = "tcp://127.0.0.1:47315",
                peerSpotPubSubEndpoint = "tcp://127.0.0.1:47317",
                logDirectory = "logs/tictactoe",
            )

        fun load(args: Array<String>): SampleSettings {
            val defaults = fromProperties(readOption(args, "--config"), createDefault())
            val resolved = fromArgs(args, defaults)

            // The sample owns its Redis via run_sample.sh/run_sample.ps1, which provisions
            // an isolated container; require the endpoint so a stray direct run never
            // silently falls back to a developer's local Redis.
            check(resolved.redisEndpoint.isNotBlank()) {
                "redisEndpoint is required; run the sample via run_sample.sh/run_sample.ps1, " +
                    "which provisions an isolated Redis container."
            }

            return resolved
        }

        private fun fromArgs(args: Array<String>, defaults: SampleSettings): SampleSettings {
            val apiBind = readOption(args, "--api-bind") ?: defaults.apiBindUrl
            return SampleSettings(
                apiBindUrl = apiBind,
                apiPublicUrl = readOption(args, "--api-url") ?: apiBind,
                apiChannelEndpoint = readOption(args, "--api-channel-endpoint") ?: defaults.apiChannelEndpoint,
                playChannelEndpoint = readOption(args, "--play-channel-endpoint") ?: defaults.playChannelEndpoint,
                playChannelEndpoints = readListOption(args, "--play-channel-endpoints", defaults.playChannelEndpoints),
                playEndpoint = readOption(args, "--play-endpoint") ?: defaults.playEndpoint,
                playEndpoints = readListOption(args, "--play-endpoints", defaults.playEndpoints),
                spotEndpoint = readOption(args, "--spot-endpoint") ?: defaults.spotEndpoint,
                spotEndpoints = readListOption(args, "--spot-endpoints", defaults.spotEndpoints),
                spotPubSubEndpoint = readOption(args, "--spot-pubsub-endpoint") ?: defaults.spotPubSubEndpoint,
                spotPubSubEndpoints = readListOption(args, "--spot-pubsub-endpoints", defaults.spotPubSubEndpoints),
                redisEndpoint = readOption(args, "--redis-endpoint") ?: defaults.redisEndpoint,
                redisKeyPrefix = readOption(args, "--redis-key-prefix") ?: defaults.redisKeyPrefix,
                playSpotNodeRid = readOption(args, "--play-spot-node-rid") ?: defaults.playSpotNodeRid,
                peerPlaySpotNodeRid = readOption(args, "--peer-play-spot-node-rid") ?: defaults.peerPlaySpotNodeRid,
                peerSpotEndpoint = readOption(args, "--peer-spot-endpoint") ?: defaults.peerSpotEndpoint,
                peerSpotPubSubEndpoint = readOption(args, "--peer-spot-pubsub-endpoint") ?: defaults.peerSpotPubSubEndpoint,
                logDirectory = readOption(args, "--log-dir") ?: defaults.logDirectory,
            )
        }

        private fun fromProperties(path: String?, defaults: SampleSettings): SampleSettings {
            if (path.isNullOrBlank()) {
                return defaults
            }
            val properties = Properties()
            Files.newInputStream(Path.of(path)).use(properties::load)
            return SampleSettings(
                apiBindUrl = properties.getProperty("sample.apiBindUrl", defaults.apiBindUrl),
                apiPublicUrl = properties.getProperty("sample.apiPublicUrl", defaults.apiPublicUrl),
                apiChannelEndpoint = properties.getProperty("sample.apiChannelEndpoint", defaults.apiChannelEndpoint),
                playChannelEndpoint = properties.getProperty("sample.playChannelEndpoint", defaults.playChannelEndpoint),
                playChannelEndpoints = readListProperty(properties, "sample.playChannelEndpoints", defaults.playChannelEndpoints),
                playEndpoint = properties.getProperty("sample.playEndpoint", defaults.playEndpoint),
                playEndpoints = readListProperty(properties, "sample.playEndpoints", defaults.playEndpoints),
                spotEndpoint = properties.getProperty("sample.spotEndpoint", defaults.spotEndpoint),
                spotEndpoints = readListProperty(properties, "sample.spotEndpoints", defaults.spotEndpoints),
                spotPubSubEndpoint = properties.getProperty("sample.spotPubSubEndpoint", defaults.spotPubSubEndpoint),
                spotPubSubEndpoints = readListProperty(properties, "sample.spotPubSubEndpoints", defaults.spotPubSubEndpoints),
                redisEndpoint = properties.getProperty("sample.redisEndpoint", defaults.redisEndpoint),
                redisKeyPrefix = properties.getProperty("sample.redisKeyPrefix", defaults.redisKeyPrefix),
                playSpotNodeRid = properties.getProperty("sample.playSpotNodeRid", defaults.playSpotNodeRid),
                peerPlaySpotNodeRid = properties.getProperty("sample.peerPlaySpotNodeRid", defaults.peerPlaySpotNodeRid),
                peerSpotEndpoint = properties.getProperty("sample.peerSpotEndpoint", defaults.peerSpotEndpoint),
                peerSpotPubSubEndpoint = properties.getProperty("sample.peerSpotPubSubEndpoint", defaults.peerSpotPubSubEndpoint),
                logDirectory = properties.getProperty("sample.logDirectory", defaults.logDirectory),
            )
        }

        private fun readOption(args: Array<String>, name: String): String? {
            val index = args.indexOf(name)
            if (index < 0) {
                return null
            }
            require(index + 1 < args.size) { "Missing value for '$name'." }
            return args[index + 1]
        }

        private fun readListOption(args: Array<String>, name: String, defaultValue: List<String>): List<String> =
            split(readOption(args, name), defaultValue)

        private fun readListProperty(properties: Properties, name: String, defaultValue: List<String>): List<String> =
            split(properties.getProperty(name), defaultValue)

        private fun split(value: String?, defaultValue: List<String>): List<String> =
            value
                ?.split(",")
                ?.map { it.trim() }
                ?.filter { it.isNotEmpty() }
                ?.takeIf { it.isNotEmpty() }
                ?: defaultValue
    }
}
