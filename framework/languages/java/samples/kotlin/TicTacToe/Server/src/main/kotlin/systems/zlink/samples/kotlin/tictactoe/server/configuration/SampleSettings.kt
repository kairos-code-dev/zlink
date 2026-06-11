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
    val playRouterEndpoint: String,
    val playEndpoint: String,
    val spotEndpoint: String,
    val logDirectory: String,
) {
    val apiHttpPort: Int
        get() = URI.create(apiBindUrl).port

    companion object {
        fun createDefault(): SampleSettings =
            SampleSettings(
                apiBindUrl = "http://127.0.0.1:18081",
                apiPublicUrl = "http://127.0.0.1:18081",
                apiChannelEndpoint = "tcp://127.0.0.1:47301",
                playChannelEndpoint = "tcp://127.0.0.1:47303",
                playRouterEndpoint = "tcp://127.0.0.1:47304",
                playEndpoint = "tcp://127.0.0.1:47302",
                spotEndpoint = "tcp://127.0.0.1:47305",
                logDirectory = "logs/tictactoe",
            )

        fun load(args: Array<String>): SampleSettings {
            val defaults = fromProperties(readOption(args, "--config"), createDefault())
            return fromArgs(args, defaults)
        }

        private fun fromArgs(args: Array<String>, defaults: SampleSettings): SampleSettings {
            val apiBind = readOption(args, "--api-bind") ?: defaults.apiBindUrl
            return SampleSettings(
                apiBindUrl = apiBind,
                apiPublicUrl = readOption(args, "--api-url") ?: apiBind,
                apiChannelEndpoint = readOption(args, "--api-channel-endpoint") ?: defaults.apiChannelEndpoint,
                playChannelEndpoint = readOption(args, "--play-channel-endpoint") ?: defaults.playChannelEndpoint,
                playRouterEndpoint = readOption(args, "--play-router-endpoint") ?: defaults.playRouterEndpoint,
                playEndpoint = readOption(args, "--play-endpoint") ?: defaults.playEndpoint,
                spotEndpoint = readOption(args, "--spot-endpoint") ?: defaults.spotEndpoint,
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
                playRouterEndpoint = properties.getProperty("sample.playRouterEndpoint", defaults.playRouterEndpoint),
                playEndpoint = properties.getProperty("sample.playEndpoint", defaults.playEndpoint),
                spotEndpoint = properties.getProperty("sample.spotEndpoint", defaults.spotEndpoint),
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
    }
}
