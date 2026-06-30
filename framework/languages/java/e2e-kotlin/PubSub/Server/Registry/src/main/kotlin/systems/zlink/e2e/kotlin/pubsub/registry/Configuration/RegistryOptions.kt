package systems.zlink.e2e.kotlin.pubsub.registry

data class RegistryOptions(
    val pubEndpoint: String,
    val routerEndpoint: String,
    val httpEndpoint: String,
) {
    companion object {
        fun parse(args: Array<String>): RegistryOptions {
            val values = parseArgs(args)
            fun required(key: String): String =
                values[key]?.takeIf { it.isNotBlank() } ?: throw IllegalArgumentException("--$key is required.")
            return RegistryOptions(
                pubEndpoint = required("registry-pub-endpoint"),
                routerEndpoint = required("registry-router-endpoint"),
                httpEndpoint = required("http-endpoint"),
            )
        }
    }
}

private fun parseArgs(args: Array<String>): Map<String, String> {
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
    return values
}
