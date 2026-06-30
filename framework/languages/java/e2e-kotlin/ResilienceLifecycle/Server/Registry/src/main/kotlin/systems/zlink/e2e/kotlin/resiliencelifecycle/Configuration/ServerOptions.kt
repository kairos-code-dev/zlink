package systems.zlink.e2e.kotlin.resiliencelifecycle

data class ServerOptions(
    val registryPubEndpoint: String,
    val registryRouterEndpoint: String,
) {
    companion object {
        fun fromEnv(): ServerOptions =
            ServerOptions(
                registryPubEndpoint = Env.get("ZLINK_KOTLIN_E2E_REGISTRY_PUB"),
                registryRouterEndpoint = Env.get("ZLINK_KOTLIN_E2E_REGISTRY_ROUTER"),
            )
    }
}
