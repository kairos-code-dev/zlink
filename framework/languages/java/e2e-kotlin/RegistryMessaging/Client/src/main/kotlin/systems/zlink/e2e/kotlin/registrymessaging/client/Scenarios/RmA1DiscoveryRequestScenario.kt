package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest

object RmA1DiscoveryRequestScenario {
    fun run(providerA: HttpJson, providerB: HttpJson, registry: HttpJson) {
        val reply = providerA.post<ProfileReply>("/profile/request", ProfileRequest("rm-a1"))
        ScenarioAssert.that(reply.value == "profile:rm-a1", "RM-A1 reply value mismatch.")
        ScenarioAssert.that(reply.providerRid == "api-a" || reply.providerRid == "api-b", "RM-A1 provider rid mismatch.")

        val topology = registry.get<List<Map<String, Any?>>>("/registry/topology")
        val ready = topology.count {
            it["channelName"] == "profile" && it["serviceRole"].toString().contains("Router", ignoreCase = true) &&
                it["state"].toString().contains("Ready", ignoreCase = true)
        }
        ScenarioAssert.that(ready >= 2, "RM-A1 expected two ready profile providers in topology.")

        val evidence = providerA.get<List<String>>("/evidence") + providerB.get<List<String>>("/evidence")
        ScenarioAssert.that(evidence.any { it.contains("value=rm-a1") }, "RM-A1 provider evidence missing.")
        println("scenario RM-A1 passed")
    }
}
