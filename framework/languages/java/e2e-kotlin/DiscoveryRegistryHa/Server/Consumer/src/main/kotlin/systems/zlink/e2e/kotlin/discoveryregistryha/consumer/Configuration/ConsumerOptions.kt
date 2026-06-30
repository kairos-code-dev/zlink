package systems.zlink.e2e.kotlin.discoveryregistryha.consumer.Configuration

import systems.zlink.e2e.kotlin.discoveryregistryha.CliOptions

data class ConsumerOptions(
    val rid: String,
    val httpEndpoint: String,
    val registryRouters: List<String>,
    val logDir: String,
) {
    companion object {
        fun parse(args: Array<String>): ConsumerOptions {
            val options = CliOptions.parse(args)
            return ConsumerOptions(
                rid = options.get("--consumer-rid", "consumer"),
                httpEndpoint = options.get("--http-endpoint"),
                registryRouters = options.csv("--registry-routers"),
                logDir = options.get("--log-dir", "logs"),
            )
        }
    }
}
