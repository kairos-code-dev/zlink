package systems.zlink.e2e.kotlin.spotservice

import systems.zlink.e2e.kotlin.spotservice.client.runClientApplication
import systems.zlink.e2e.kotlin.spotservice.play.PlayApplication
import systems.zlink.e2e.kotlin.spotservice.publisher.PublisherApplication
import systems.zlink.e2e.kotlin.spotservice.registry.RegistryApplication
import systems.zlink.e2e.kotlin.spotservice.session.SessionApplication

fun main(args: Array<String>) {
    when (Env.get("ZLINK_KOTLIN_E2E_ROLE", "client")) {
        "registry" -> RegistryApplication.run(*args)
        "play" -> PlayApplication.run(*args)
        "publisher" -> PublisherApplication.run(*args)
        "client" -> runClientApplication(*args)
        "session" -> SessionApplication.run(*args)
        else -> throw IllegalArgumentException("unknown role ${Env.get("ZLINK_KOTLIN_E2E_ROLE")}")
    }
}
