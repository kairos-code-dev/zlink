package systems.zlink.e2e.kotlin.discoveryregistryha

fun main(args: Array<String>) {
    when (Env.get("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "registry" -> RegistryApplication.run(*args)
        "provider" -> ProviderApplication.run(*args)
        "embedded" -> {
            RegistryApplication.run(*args)
            ProviderApplication.run(*args)
        }
        "client" -> ClientApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${Env.get("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}
