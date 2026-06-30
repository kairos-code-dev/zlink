package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.RequestFailureResult

object RmC4TimeoutIsolationScenario {
    fun run(discoveryConsumer: HttpJson, providerA: HttpJson, providerB: HttpJson) {
        val timeout = discoveryConsumer.post<RequestFailureResult>("/profile/slow-request", ProfileRequest("slow"))
        ScenarioAssert.that(timeout.failed, "RM-C4 expected the slow request to time out.")

        val immediate = discoveryConsumer.post<ProfileReply>("/profile/request", ProfileRequest("rm-c4-after-timeout"))
        ScenarioAssert.that(immediate.value == "profile:rm-c4-after-timeout", "RM-C4 follow-up reply mismatch.")
        Thread.sleep(1200)
        val later = discoveryConsumer.post<ProfileReply>("/profile/request", ProfileRequest("rm-c4-later"))
        ScenarioAssert.that(later.value == "profile:rm-c4-later", "RM-C4 later reply mismatch.")

        val evidence = providerA.get<List<String>>("/evidence") + providerB.get<List<String>>("/evidence")
        ScenarioAssert.that(
            evidence.any { it.contains("rm-c4-after-timeout") } && evidence.any { it.contains("rm-c4-later") },
            "RM-C4 follow-up evidence missing.",
        )
        println("scenario RM-C4 passed")
    }
}
