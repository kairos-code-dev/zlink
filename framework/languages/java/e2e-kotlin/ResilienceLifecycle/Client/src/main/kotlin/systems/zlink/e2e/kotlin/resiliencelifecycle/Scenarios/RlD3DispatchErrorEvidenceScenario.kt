package systems.zlink.e2e.kotlin.resiliencelifecycle

import java.time.Duration

fun ClientScenarioContext.runDispatchErrorEvidenceScenario() {
    try {
        client.requestToChannel(Contracts.CHANNEL, Contracts.UnhandledRequest("d3-missing-handler"))
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.WorkReply::class.java)
        throw IllegalStateException("RL-D3 missing handler request unexpectedly completed")
    } catch (_: RuntimeException) {
        waitForDispatchErrorAny("UnhandledRequest", adminA(), adminB())
    }
    val followUp = client.requestToChannel(Contracts.CHANNEL, Contracts.WorkRequest("d3-follow-up"))
        .timeout(Duration.ofSeconds(3))
        .await(Contracts.WorkReply::class.java)
    ensure(followUp.value() == "work:d3-follow-up", "RL-D3 follow-up payload mismatch")
    println("scenario RL-D3 passed")
}
