package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ClientOptions
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.DynamicClusterLauncher
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest

object RmB1ScaleOutScenario {
    fun run(options: ClientOptions) {
        DynamicClusterLauncher.start(options, "rm-b1").use { cluster ->
            val providerA = cluster.startProvider("api-a-scale-out", "api-a")
            val requester = HttpJson(providerA.httpUrl)
            repeat(5) { index ->
                val reply = requester.post<ProfileReply>("/profile/request", ProfileRequest("scale-out-before-$index"))
                ScenarioAssert.that(reply.providerRid == "api-a", "RM-B1 initial traffic should only use api-a.")
            }
            cluster.startProvider("api-b-scale-out", "api-b")
            Thread.sleep(3000)
            val providers = mutableSetOf<String>()
            var index = 0
            while (index < 100 && providers.size < 2) {
                providers += requester.post<ProfileReply>(
                    "/profile/request",
                    ProfileRequest("scale-out-after-$index"),
                ).providerRid
                index++
            }
            ScenarioAssert.that(providers.contains("api-a") && providers.contains("api-b"), "RM-B1 did not route to both providers after scale-out.")
        }
        println("scenario RM-B1 passed")
    }
}
