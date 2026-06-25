package systems.zlink.e2e.kotlin.registrationcodec

fun main(args: Array<String>) {
    when (Env.get("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "server" -> runServerApplication(*args)
        "invalid-server" -> runInvalidServerApplication(*args)
        "client" -> runClientApplication(*args)
        else -> throw IllegalArgumentException("unknown role ${Env.get("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}
