package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.EvidenceWaitRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileCommand
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.RequestFailureResult

object RmC5MissingPacketScenario {
    fun run(discoveryConsumer: HttpJson, providerA: HttpJson, providerB: HttpJson) {
        val missing = discoveryConsumer.post<RequestFailureResult>("/profile/missing-request", ProfileRequest("missing-request"))
        ScenarioAssert.that(missing.failed, "RM-C5 missing request should fail.")
        discoveryConsumer.post<Map<String, Any>>("/profile/missing-command", ProfileCommand("missing-send"))

        val evidence = providerA.post<List<String>>("/evidence/wait", EvidenceWaitRequest("MissingProfileRequest")) +
            providerB.post<List<String>>("/evidence/wait", EvidenceWaitRequest("MissingProfileCommand"))
        ScenarioAssert.that(
            evidence.any { it.contains("dispatch-error") && it.contains("MissingProfileRequest") },
            "RM-C5 missing request evidence missing.",
        )
        ScenarioAssert.that(
            evidence.any { it.contains("dispatch-error") && it.contains("MissingProfileCommand") },
            "RM-C5 missing send evidence missing.",
        )

        val reply = discoveryConsumer.post<ProfileReply>("/profile/request", ProfileRequest("rm-c5-after"))
        ScenarioAssert.that(reply.value == "profile:rm-c5-after", "RM-C5 normal request after negative path failed.")
        println("scenario RM-C5 passed")
    }
}
