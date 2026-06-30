package systems.zlink.e2e.kotlin.registrymessaging.registry.Configuration

data class ServerOptions(
    val rid: String,
    val httpUrl: String,
    val logDir: String,
    val evidenceFile: String?,
    val registryPubEndpoint: String,
    val registryRouterEndpoint: String,
) {
    companion object {
        fun parse(args: Array<String>): ServerOptions {
            val values = linkedMapOf<String, String>()
            var index = 0
            while (index < args.size) {
                val key = args[index]
                if (!key.startsWith("--")) {
                    index++
                    continue
                }
                require(index + 1 < args.size) { "Missing value for $key." }
                values[key.removePrefix("--")] = args[index + 1]
                index += 2
            }
            return ServerOptions(
                rid = values["rid"] ?: "registry",
                httpUrl = values["http-url"] ?: "http://127.0.0.1:0",
                logDir = values["log-dir"] ?: System.getProperty("java.io.tmpdir") + "/zlink-kotlin-e2e-log",
                evidenceFile = values["evidence-file"],
                registryPubEndpoint = values["registry-pub-endpoint"]
                    ?: throw IllegalArgumentException("--registry-pub-endpoint is required."),
                registryRouterEndpoint = values["registry-router-endpoint"]
                    ?: throw IllegalArgumentException("--registry-router-endpoint is required."),
            )
        }
    }
}
