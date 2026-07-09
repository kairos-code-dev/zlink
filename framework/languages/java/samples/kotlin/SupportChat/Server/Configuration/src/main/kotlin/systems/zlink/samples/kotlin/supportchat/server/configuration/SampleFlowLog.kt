package systems.zlink.samples.kotlin.supportchat.server.configuration

object SampleFlowLog {
    fun path(role: String): String =
        "${System.getenv("SUPPORTCHAT_LOG_DIR")?.takeIf { it.isNotBlank() } ?: "logs"}/flow-$role.log"
}
