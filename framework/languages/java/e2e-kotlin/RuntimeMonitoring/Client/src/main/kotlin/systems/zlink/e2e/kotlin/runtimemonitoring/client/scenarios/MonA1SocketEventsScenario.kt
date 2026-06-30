package systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios

import java.time.Duration
import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.client.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class MonA1SocketEventsScenario(
    private val client: ZLinkClient,
) {
    fun run() {
        repeat(5) { index ->
            val reply = client.requestToChannel(
                Contracts.CHANNEL,
                Contracts.WorkRequest("a1-$index"),
            )
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkReply::class.java)
            ScenarioAssert.ensure(reply.value == "work:a1-$index", "reply mismatch")
        }
        println("scenario MON-A1 passed")
    }
}
