package systems.zlink.e2e.kotlin.spotservice.client

import systems.zlink.e2e.kotlin.spotservice.Env

fun main(args: Array<String>) {
    val mode = Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "all")
    ClientScenario().runMode(mode)
    println("spot-service kotlin e2e mode=$mode result=passed")
}
