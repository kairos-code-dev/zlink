package systems.zlink.e2e.kotlin.discoveryregistryha.probe.Configuration

import systems.zlink.e2e.kotlin.discoveryregistryha.CliOptions

data class ProbeOptions(
    val httpEndpoint: String,
    val registryRouter: String,
    val logDir: String,
) {
    companion object {
        fun parse(args: Array<String>): ProbeOptions {
            val options = CliOptions.parse(args)
            return ProbeOptions(
                httpEndpoint = options.get("--http-endpoint"),
                registryRouter = options.get("--registry-router"),
                logDir = options.get("--log-dir", "logs"),
            )
        }
    }
}
