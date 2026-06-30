package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.EvidenceWaitRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest

object RmA2ManualEndpointScenario {
    fun run(providerA: HttpJson) {
        val reply = providerA.post<ProfileReply>("/profile/manual", ProfileRequest("rm-a2"))
        ScenarioAssert.that(reply.value == "profile:rm-a2", "RM-A2 reply value mismatch.")
        ScenarioAssert.that(reply.providerRid == "api-a", "RM-A2 manual endpoint should reach api-a.")
        val evidence = providerA.post<List<String>>("/evidence/wait", EvidenceWaitRequest("value=rm-a2"))
        ScenarioAssert.that(evidence.any { it.contains("value=rm-a2") }, "RM-A2 api-a evidence missing.")
        println("scenario RM-A2 passed")
    }
}
