package systems.zlink.e2e.kotlin.registrymessaging.workflow.Configuration

data class ServerOptions(
    val rid: String,
    val httpUrl: String,
    val logDir: String,
    val evidenceFile: String?,
    val registryRouterEndpoint: String,
    val workflowEndpoint: String,
    val weight: Int,
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
                rid = values["rid"] ?: "workflow",
                httpUrl = values["http-url"] ?: "http://127.0.0.1:0",
                logDir = values["log-dir"] ?: System.getProperty("java.io.tmpdir") + "/zlink-kotlin-e2e-log",
                evidenceFile = values["evidence-file"],
                registryRouterEndpoint = values["registry-router-endpoint"]
                    ?: throw IllegalArgumentException("--registry-router-endpoint is required."),
                workflowEndpoint = values["workflow-endpoint"]
                    ?: throw IllegalArgumentException("--workflow-endpoint is required."),
                weight = (values["weight"] ?: "100").toInt(),
            )
        }
    }
}
