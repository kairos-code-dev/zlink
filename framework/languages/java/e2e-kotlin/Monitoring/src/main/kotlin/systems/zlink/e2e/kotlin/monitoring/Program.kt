package systems.zlink.e2e.kotlin.monitoring

fun main(args: Array<String>) {
    when (Env.get("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "registry" -> RegistryApplication.run(*args)
        "service" -> ServiceApplication.run(*args)
        "client" -> ClientApplication.run(*args)
        "validation" -> MonitoringValidationApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${Env.get("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}
