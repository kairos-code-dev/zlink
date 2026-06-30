package systems.zlink.e2e.kotlin.resiliencelifecycle

import java.time.Duration

fun ClientScenarioContext.runGracefulShutdownScenario() {
    waitForTopology(2)
    val beforeShutdown = client.requestToChannel(Contracts.CHANNEL, Contracts.WorkRequest("b3-before-shutdown"))
        .timeout(Duration.ofSeconds(3))
        .await(Contracts.WorkReply::class.java)
    ensure(beforeShutdown.value() == "work:b3-before-shutdown", "RL-B3 pre-shutdown reply payload mismatch")

    post("${adminB()}/admin/shutdown")
    waitForTopology(1)
    val providers = collectStableProvidersWithout("b3-after-shutdown", "api-b", "api-a")
    ensure(providers.contains("api-a"), "RL-B3 did not converge to api-a after api-b shutdown")
    println("scenario RL-B3 passed")
}
