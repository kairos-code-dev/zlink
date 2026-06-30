package systems.zlink.e2e.kotlin.discoveryregistryha

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.BasicDiscoveryScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrA2ClusterBridgeScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrA3ClusterBridgeScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrA4ThirdRegistryScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrB1FailoverScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrB2FailoverScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrB3RecoveryScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrC1EmbeddedRegistryScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrC2EmbeddedRegistryScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrC3EmbeddedRegistryScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrD1DirectEndpointScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrD2DirectEndpointScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrD3DirectEndpointScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Scenarios.DrD4DirectEndpointScenario
import systems.zlink.e2e.kotlin.discoveryregistryha.client.Support.ClientScenarioContext

class ClientScenario(
    json: ObjectMapper,
    options: ClientOptions,
) {
    private val context = ClientScenarioContext(json, options)

    fun run() {
        when (context.options.scenario()) {
            "DR-A1" -> BasicDiscoveryScenario.run(context)
            "DR-A2" -> DrA2ClusterBridgeScenario.run(context)
            "DR-A3" -> DrA3ClusterBridgeScenario.run(context)
            "DR-A4" -> DrA4ThirdRegistryScenario.run(context)
            "DR-B1" -> DrB1FailoverScenario.run(context)
            "DR-B2" -> DrB2FailoverScenario.run(context)
            "DR-B3" -> DrB3RecoveryScenario.run(context)
            "DR-C1" -> DrC1EmbeddedRegistryScenario.run(context)
            "DR-C2" -> DrC2EmbeddedRegistryScenario.run(context)
            "DR-C3" -> DrC3EmbeddedRegistryScenario.run(context)
            "DR-D1" -> DrD1DirectEndpointScenario.run(context)
            "DR-D2" -> DrD2DirectEndpointScenario.run(context)
            "DR-D3" -> DrD3DirectEndpointScenario.run(context)
            "DR-D4" -> DrD4DirectEndpointScenario.run(context)
            else -> error("unknown scenario: ${context.options.scenario()}")
        }
    }
}
