package systems.zlink.e2e.kotlin.spotservice

fun main(args: Array<String>) {
    when (Env.get("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "registry" -> RegistryApplication.run(*args)
        "play" -> PlayApplication.run(*args)
        "publisher" -> PublisherApplication.run(*args)
        "client" -> ClientApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${Env.get("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}
