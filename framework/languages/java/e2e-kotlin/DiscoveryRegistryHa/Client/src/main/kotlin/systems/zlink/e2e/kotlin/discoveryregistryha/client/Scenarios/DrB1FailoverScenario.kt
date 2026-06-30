package systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios

import systems.zlink.e2e.kotlin.discoveryregistryha.client.Support.ClientScenarioContext

object DrB1FailoverScenario {
    fun run(context: ClientScenarioContext) {
        context.runChannelScenario()
    }
}
