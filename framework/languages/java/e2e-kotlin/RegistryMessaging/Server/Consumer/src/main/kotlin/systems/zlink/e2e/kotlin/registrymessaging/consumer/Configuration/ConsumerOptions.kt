package systems.zlink.e2e.kotlin.registrymessaging.consumer.Configuration

data class ConsumerOptions(
    val httpUrl: String,
    val logDir: String,
    val traceLabel: String,
    val providerEndpoints: List<String>,
    val registryRouterEndpoint: String?,
) {
    companion object {
        fun parse(args: Array<String>): ConsumerOptions {
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
            val endpoints = values["provider-endpoint"]?.filter { it.isNotBlank() }.orEmpty()
            val registry = values["registry-router-endpoint"]?.lastOrNull()?.takeIf { it.isNotBlank() }
            require(endpoints.isNotEmpty() || !registry.isNullOrBlank()) {
                "--provider-endpoint or --registry-router-endpoint is required."
            }
            return ConsumerOptions(
                httpUrl = values.last("http-url", "http://127.0.0.1:0"),
                logDir = values.last("log-dir", System.getProperty("java.io.tmpdir") + "/zlink-kotlin-e2e-log"),
                traceLabel = values.last("trace-label", "consumer"),
                providerEndpoints = endpoints,
                registryRouterEndpoint = registry,
            )
        }
    }
}

private fun Map<String, List<String>>.last(key: String, defaultValue: String): String =
    this[key]?.lastOrNull()?.takeIf { it.isNotBlank() } ?: defaultValue
