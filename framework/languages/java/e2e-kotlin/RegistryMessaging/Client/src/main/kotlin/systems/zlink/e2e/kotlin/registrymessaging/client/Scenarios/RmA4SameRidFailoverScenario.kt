package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import java.util.UUID
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ClientOptions
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.DynamicClusterLauncher
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.EvidenceWaitRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest

object RmA4SameRidFailoverScenario {
    fun run(options: ClientOptions) {
        DynamicClusterLauncher.start(options, "rm-a4").use { cluster ->
            val providerV1 = cluster.startProvider("api-a-v1", "api-a")
            val providerV1Http = HttpJson(providerV1.httpUrl)
            val first = providerV1Http.post<ProfileReply>("/profile/request", ProfileRequest("rm-a4-v1"))
            ScenarioAssert.that(first.providerRid == "api-a", "RM-A4 initial request should reach api-a.")
            providerV1Http.post<List<String>>("/evidence/wait", EvidenceWaitRequest("value=rm-a4-v1"))

            cluster.stop(providerV1)
            val providerV2 = cluster.startProvider("api-a-v2", "api-a")
            val providerV2Http = HttpJson(providerV2.httpUrl)
            val marker = "rm-a4-${UUID.randomUUID().toString().replace("-", "")}"
            repeat(20) { index ->
                val reply = providerV2Http.post<ProfileReply>("/profile/request", ProfileRequest("$marker-$index"))
                ScenarioAssert.that(reply.providerRid == "api-a", "RM-A4 replacement request should reach api-a.")
            }
            val evidence = providerV2Http.post<List<String>>("/evidence/wait", EvidenceWaitRequest("$marker-19"))
            ScenarioAssert.that(evidence.any { it.contains("$marker-19") }, "RM-A4 replacement evidence missing.")
        }
        println("scenario RM-A4 passed")
    }
}
