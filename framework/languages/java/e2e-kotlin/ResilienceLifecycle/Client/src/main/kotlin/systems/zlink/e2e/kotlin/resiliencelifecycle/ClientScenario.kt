package systems.zlink.e2e.kotlin.resiliencelifecycle

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.registry.ZLinkRegistryQueryClient

class ClientScenario(
    client: ZLinkClient,
    registry: ZLinkRegistryQueryClient?,
    json: ObjectMapper,
    options: ClientOptions,
) {
    private val context = ClientScenarioContext(client, registry, json, options)

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
