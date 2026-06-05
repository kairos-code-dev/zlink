package systems.zlink.samples.kotlin.tictactoe.server.configuration

import java.net.URI

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

    fun withEphemeralDefaults(): SampleSettings {
        val apiPort = SamplePorts.reserve()
        return copy(
            apiBindUrl = "http://127.0.0.1:$apiPort",
            apiPublicUrl = "http://127.0.0.1:$apiPort",
            apiChannelEndpoint = "tcp://127.0.0.1:${SamplePorts.reserve()}",
            playChannelEndpoint = "tcp://127.0.0.1:${SamplePorts.reserve()}",
            playRouterEndpoint = "tcp://127.0.0.1:${SamplePorts.reserve()}",
            playEndpoint = "tcp://127.0.0.1:${SamplePorts.reserve()}",
            spotEndpoint = "tcp://127.0.0.1:${SamplePorts.reserve()}",
        )
    }

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

        fun fromArgs(args: Array<String>): SampleSettings {
            val defaults = createDefault()
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
