package systems.zlink.e2e.kotlin.registrationcodec.client.support

data class ClientOptions(
    val serverEndpoint: String,
    val httpEndpoint: String,
    val logDir: String,
    val mode: String,
) {
    companion object {
        fun parse(args: Array<String>): ClientOptions {
            val values = mutableMapOf<String, MutableList<String>>()
            var index = 0
            while (index < args.size) {
                val key = args[index]
                require(key.startsWith("--")) { "Unexpected argument '$key'." }
                require(index + 1 < args.size) { "Missing value for '$key'." }
                values.getOrPut(key) { mutableListOf() }.add(args[index + 1])
                index += 2
            }

            fun get(name: String): String? = values[name]?.lastOrNull()
            return ClientOptions(
                serverEndpoint = get("--server-endpoint") ?: "",
                httpEndpoint = get("--http-endpoint") ?: "",
                logDir = get("--log-dir") ?: "logs",
                mode = get("--mode") ?: "",
            )
        }
    }
}
