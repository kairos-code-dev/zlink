package systems.zlink.e2e.kotlin.resiliencelifecycle

data class ClientOptions(
    val mode: String,
    val registryRouterEndpoint: String,
    val consumerHttpEndpoint: String,
    val logDir: String,
    val httpAEndpoint: String?,
    val httpBEndpoint: String?,
    val apiAReplacementEndpoint: String?,
    val controlDir: String?,
    val scenario: String,
    val stormExitDelayMillis: Long,
) {
    companion object {
        fun fromEnv(): ClientOptions =
            ClientOptions(
                mode = Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "default"),
                registryRouterEndpoint = Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"),
                consumerHttpEndpoint = Env.get("ZLINK_KOTLIN_E2E_CONSUMER_HTTP_ENDPOINT"),
                logDir = Env.get("ZLINK_KOTLIN_E2E_LOG_DIR", "logs"),
                httpAEndpoint = optional("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT"),
                httpBEndpoint = optional("ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT"),
                apiAReplacementEndpoint = optional("ZLINK_KOTLIN_E2E_API_A_REPLACEMENT_ENDPOINT"),
                controlDir = optional("ZLINK_KOTLIN_E2E_CONTROL_DIR"),
                scenario = Env.get("ZLINK_KOTLIN_E2E_SCENARIO", "all"),
                stormExitDelayMillis = Env.get("ZLINK_KOTLIN_E2E_STORM_EXIT_DELAY_MS", "0").toLong(),
            )

        private fun optional(name: String): String? =
            System.getenv(name)?.takeIf { it.isNotBlank() }
    }
}
