package systems.zlink.e2e.kotlin.runtimemonitoring.client.scenarios

import systems.zlink.e2e.kotlin.runtimemonitoring.client.ClientOptions
import systems.zlink.e2e.kotlin.runtimemonitoring.client.MonitoringEvidenceClient
import systems.zlink.e2e.kotlin.runtimemonitoring.client.ScenarioAssert

class MonB2RegistrationValidationScenario(
    private val options: ClientOptions,
    private val evidence: MonitoringEvidenceClient,
) {
    fun run() {
        val scenarioEvidence = listOf(
            evidence.post("${options.triggerHttp}/validation/registration/duplicate-source"),
            evidence.post("${options.triggerHttp}/validation/registration/interval"),
            evidence.post("${options.triggerHttp}/validation/registration/missing-spot"),
            evidence.post("${options.triggerHttp}/validation/registration/missing-socket"),
        )

        ScenarioAssert.ensure(
            scenarioEvidence.any { line ->
                line.contains("mon-b2|duplicate=") &&
                    line.contains("Duplicate monitoring socket source")
            },
            "MON-B2 duplicate source validation evidence missing",
        )
        ScenarioAssert.ensure(
            scenarioEvidence.any { line ->
                line.contains("mon-b2|interval=") &&
                    line.contains("location runtime interval must be positive")
            },
            "MON-B2 interval validation evidence missing",
        )
        ScenarioAssert.ensure(
            scenarioEvidence.any { line ->
                line.contains("mon-b2|missing-spot=") &&
                    line.contains("monitoring spot source is not configured")
            },
            "MON-B2 missing spot validation evidence missing",
        )
        ScenarioAssert.ensure(
            scenarioEvidence.any { line ->
                line.contains("mon-b2|missing-socket=") &&
                    line.contains("monitoring socket source is not configured")
            },
            "MON-B2 missing socket validation evidence missing",
        )
        println("scenario MON-B2 passed")
    }
}
