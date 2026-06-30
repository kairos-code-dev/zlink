package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import java.util.concurrent.CompletableFuture
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.BackpressureRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.EvidenceWaitReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileMsg
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRes

object RmC9BackpressureScenario {
    private const val SLOW_SEND_COUNT = 8

    fun run(backpressureConsumer: HttpJson, providerA: HttpJson) {
        backpressureConsumer.post<Map<String, String>>("/profile/backpressure/reset")
        val futures = (0 until SLOW_SEND_COUNT).map { index ->
            CompletableFuture.supplyAsync {
                backpressureConsumer.post<BackpressureRes>(
                    "/profile/backpressure/send",
                    ProfileMsg("rm-c9-slow-$index"),
                ).outcome
            }
        }
        val outcomes = futures.map { it.join() }
        ScenarioAssert.that(
            outcomes.all { it == "Submitted" },
            "RM-C9 expected all one-way sends to be submitted without a public bounded-failure oracle.",
        )

        Thread.sleep(10000)
        val recovered = backpressureConsumer.post<ProfileRes>("/profile/request", ProfileReq("rm-c9-after"))
        ScenarioAssert.that(recovered.value == "profile:rm-c9-after", "RM-C9 connection did not recover after pressure.")
        val evidence = providerA.post<List<String>>("/evidence/wait", EvidenceWaitReq("rm-c9-after", 20000))
        ScenarioAssert.that(evidence.any { it.contains("rm-c9-after") }, "RM-C9 recovery evidence missing.")
        println("scenario RM-C9 passed")
    }
}
