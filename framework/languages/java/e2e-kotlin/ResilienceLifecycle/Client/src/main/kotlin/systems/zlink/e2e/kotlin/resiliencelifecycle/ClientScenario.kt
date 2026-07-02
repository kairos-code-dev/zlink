package systems.zlink.e2e.kotlin.resiliencelifecycle

import com.fasterxml.jackson.databind.ObjectMapper

class ClientScenario(
    json: ObjectMapper,
    options: ClientOptions,
) {
    private val context = ClientScenarioContext(json, options)

    fun run(mode: String) {
        when (mode) {
            "restart" -> context.runServerRestartScenario()
            "reschedule" -> context.runProviderEndpointRemapScenario()
            "flapping" -> context.runProviderFlappingScenario()
            "storm" -> context.runReconnectStormScenario()
            "cleanup" -> context.runClientHostLifecycleAndMixedBurstScenario()
            else -> {
                runDefaultScenarios()
            }
        }
    }

    private fun runDefaultScenarios() {
        when (context.options.scenario) {
            "all" -> {
                context.runClientTimeoutCleanupScenario()
                context.runRuntimeDrainScenario()
                context.runDrainInflightScenario()
                context.runDispatchErrorEvidenceScenario()
                context.runGrayFaultScenario()
                context.runGracefulShutdownScenario()
            }
            "RL-B1" -> context.runClientTimeoutCleanupScenario()
            "RL-B4" -> context.runRuntimeDrainScenario()
            "RL-B5" -> context.runDrainInflightScenario()
            "RL-D3" -> context.runDispatchErrorEvidenceScenario()
            "RL-B6" -> context.runGrayFaultScenario()
            "RL-B3" -> context.runGracefulShutdownScenario()
            else -> throw IllegalArgumentException("unknown ResilienceLifecycle scenario ${context.options.scenario}")
        }
    }
}
