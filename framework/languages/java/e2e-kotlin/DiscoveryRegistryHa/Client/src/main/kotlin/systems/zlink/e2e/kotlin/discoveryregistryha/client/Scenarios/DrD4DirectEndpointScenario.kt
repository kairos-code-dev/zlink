package systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios

import systems.zlink.e2e.kotlin.discoveryregistryha.client.Support.ClientScenarioContext

object DrD4DirectEndpointScenario {
    fun run(context: ClientScenarioContext) {
        context.runChannelScenario(expectTopologyMatch = true)
    }
}
