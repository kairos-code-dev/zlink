package systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios

import systems.zlink.e2e.kotlin.discoveryregistryha.client.Support.ClientScenarioContext

object DrC1EmbeddedRegistryScenario {
    fun run(context: ClientScenarioContext) {
        context.runChannelScenario(expectDeadProbeFailure = true)
    }
}
