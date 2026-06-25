package systems.zlink.e2e.kotlin.pubsub

fun main(args: Array<String>) {
    when (Env.get("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "registry" -> runRegistryApplication(*args)
        "subscriber" -> runSubscriberApplication(*args)
        "publisher", "client" -> runClientApplication(*args)
        else -> throw IllegalArgumentException("unknown role ${Env.get("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}
