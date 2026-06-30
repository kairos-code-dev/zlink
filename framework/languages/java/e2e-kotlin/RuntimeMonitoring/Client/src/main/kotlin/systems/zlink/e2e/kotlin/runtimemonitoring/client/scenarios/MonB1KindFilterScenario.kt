package systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios

import java.time.Duration
import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.client.ClientOptions
import systems.zlink.e2e.kotlin.runtimemonitoring.client.MonitoringEvidenceClient
import systems.zlink.e2e.kotlin.runtimemonitoring.client.ScenarioAssert
import systems.zlink.framework.channels.ZLinkClient

class MonB1KindFilterScenario(
    private val client: ZLinkClient,
    private val options: ClientOptions,
    private val evidence: MonitoringEvidenceClient,
) {
    fun run() {
        repeat(4) { index ->
            val reply = client.requestToChannel(
                Contracts.CHANNEL,
                Contracts.WorkRequest("b1-filter-$index"),
            )
                .timeout(Duration.ofSeconds(3))
                .await(Contracts.WorkReply::class.java)
            ScenarioAssert.ensure(
                reply.value == "work:b1-filter-$index",
                "MON-B1 filtered request reply mismatch",
            )
        }

        evidence.waitForEvent(
            options.filteredServiceHttp,
            "socket",
            Contracts.CHANNEL,
            setOf("CONNECTION_READY"),
        )
        val observed = evidence.events(options.filteredServiceHttp, "socket", Contracts.CHANNEL)
        ScenarioAssert.ensure(
            observed.contains("CONNECTION_READY"),
            "MON-B1 did not observe filtered CONNECTION_READY event",
        )
        ScenarioAssert.ensure(
            observed == setOf("CONNECTION_READY"),
            "MON-B1 socket filter allowed unexpected events: $observed",
        )
        println("scenario MON-B1 passed")
    }
}
