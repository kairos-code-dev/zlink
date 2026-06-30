package systems.zlink.e2e.kotlin.resiliencelifecycle

import java.time.Duration

fun ClientScenarioContext.runClientTimeoutCleanupScenario() {
    try {
        client.requestToChannel(Contracts.CHANNEL, Contracts.WorkReq("timeout"))
            .timeout(Duration.ofMillis(300))
            .await(Contracts.WorkRes::class.java)
        throw IllegalStateException("RL-B1 timeout request unexpectedly completed")
    } catch (_: RuntimeException) {
        waitForEvidenceAny("TimeoutStarted", adminA(), adminB())
    }
    sleep(1800)
    val followUp = client.requestToChannel(Contracts.CHANNEL, Contracts.WorkReq("b1-follow-up"))
        .timeout(Duration.ofSeconds(3))
        .await(Contracts.WorkRes::class.java)
    ensure(followUp.value() == "work:b1-follow-up", "RL-B1 follow-up payload mismatch")
    println("scenario RL-B1 passed")
}
