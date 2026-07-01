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
                context.runClientTimeoutCleanupScenario()
                context.runRuntimeDrainScenario()
                context.runDrainInflightScenario()
                context.runDispatchErrorEvidenceScenario()
                context.runGrayFaultScenario()
                context.runGracefulShutdownScenario()
            }
        }
    }
}
