package systems.zlink.e2e.kotlin.registrymessaging.provider.Configuration

data class ServerOptions(
    val rid: String,
    val httpUrl: String,
    val logDir: String,
    val evidenceFile: String?,
    val registryRouterEndpoint: String,
    val channelEndpoint: String?,
    val manualClientEndpoint: String?,
    val routeEndpoint: String?,
    val weight: Int,
    val routePeers: List<String>,
) {
    companion object {
        fun parse(args: Array<String>): ServerOptions {
            val values = linkedMapOf<String, MutableList<String>>()
            var index = 0
            while (index < args.size) {
                val key = args[index]
                if (!key.startsWith("--")) {
                    index++
                    continue
                }
                require(index + 1 < args.size) { "Missing value for $key." }
                values.getOrPut(key.removePrefix("--")) { mutableListOf() }.add(args[index + 1])
                index += 2
            }
            val rid = values.last("rid", "node")
            System.setProperty("zlink.e2e.rid", rid)
            return ServerOptions(
                rid = rid,
                httpUrl = values.last("http-url", "http://127.0.0.1:0"),
                logDir = values.last("log-dir", System.getProperty("java.io.tmpdir") + "/zlink-kotlin-e2e-log"),
                evidenceFile = values.lastOrNull("evidence-file"),
                registryRouterEndpoint = values.lastRequired("registry-router-endpoint"),
                channelEndpoint = values.lastOrNull("channel-endpoint"),
                manualClientEndpoint = values.lastOrNull("manual-client-endpoint"),
                routeEndpoint = values.lastOrNull("route-endpoint"),
                weight = values.last("weight", "100").toInt(),
                routePeers = values["route-peer"]?.toList().orEmpty(),
            )
        }
    }
}

private fun Map<String, List<String>>.last(key: String, defaultValue: String): String =
    this[key]?.lastOrNull()?.takeIf { it.isNotBlank() } ?: defaultValue

private fun Map<String, List<String>>.lastOrNull(key: String): String? =
    this[key]?.lastOrNull()?.takeIf { it.isNotBlank() }

private fun Map<String, List<String>>.lastRequired(key: String): String =
    lastOrNull(key) ?: throw IllegalArgumentException("--$key is required.")
