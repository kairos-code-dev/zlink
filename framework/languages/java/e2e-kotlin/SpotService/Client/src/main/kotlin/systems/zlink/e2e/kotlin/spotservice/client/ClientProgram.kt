package systems.zlink.e2e.kotlin.spotservice.client

import systems.zlink.e2e.kotlin.spotservice.Env
import kotlinx.coroutines.runBlocking

fun main(args: Array<String>) {
    val mode = Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "all")
    if (mode in setOf("route-mesh", "route-lifecycle", "spot-outbound", "spot-to-spot", "actor-session")) {
        ClientApplication.run(*args)
        return
    }
    runBlocking { ClientScenario().runMode(mode) }
    println("spot-service kotlin e2e mode=$mode result=passed")
}
