package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ClientOptions
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.DynamicClusterLauncher
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest

object RmB2ScaleInScenario {
    fun run(options: ClientOptions) {
        DynamicClusterLauncher.start(options, "rm-b2").use { cluster ->
            val providerA = cluster.startProvider("api-a-scale-in", "api-a")
            val providerB = cluster.startProvider("api-b-scale-in", "api-b")
            val requester = HttpJson(providerA.httpUrl)
            val before = mutableSetOf<String>()
            var index = 0
            while (index < 100 && before.size < 2) {
                before += requester.post<ProfileReply>(
                    "/profile/request",
                    ProfileRequest("scale-in-before-$index"),
                ).providerRid
                index++
            }
            ScenarioAssert.that(before.contains("api-a") && before.contains("api-b"), "RM-B2 did not start with both providers.")
            cluster.stop(providerB)
            Thread.sleep(3000)
            repeat(20) { afterIndex ->
                val reply = requester.post<ProfileReply>("/profile/request", ProfileRequest("scale-in-after-$afterIndex"))
                ScenarioAssert.that(reply.providerRid == "api-a", "RM-B2 routed to removed provider.")
            }
        }
        println("scenario RM-B2 passed")
    }
}
