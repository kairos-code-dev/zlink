package systems.zlink.e2e.kotlin.registrymessaging.client.Support

data class ClientOptions(
    val registryUrl: String,
    val providerAUrl: String,
    val providerBUrl: String,
    val workflowUrl: String,
    val directConsumerUrl: String,
    val singleConsumerUrl: String,
    val discoveryConsumerUrl: String,
    val backpressureConsumerUrl: String,
    val registryBin: String,
    val providerBin: String,
    val logDir: String,
    val scenario: String,
) {
    companion object {
        fun parse(args: Array<String>): ClientOptions {
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
            fun required(key: String): String =
                values[key]?.takeIf { it.isNotBlank() } ?: throw IllegalArgumentException("--$key is required.")
            return ClientOptions(
                registryUrl = required("registry-url"),
                providerAUrl = required("provider-a-url"),
                providerBUrl = required("provider-b-url"),
                workflowUrl = required("workflow-url"),
                directConsumerUrl = required("direct-consumer-url"),
                singleConsumerUrl = required("single-consumer-url"),
                discoveryConsumerUrl = required("discovery-consumer-url"),
                backpressureConsumerUrl = required("backpressure-consumer-url"),
                registryBin = required("registry-bin"),
                providerBin = required("provider-bin"),
                logDir = required("log-dir"),
                scenario = values["scenario"]?.takeIf { it.isNotBlank() } ?: "all",
            )
        }
    }
}
