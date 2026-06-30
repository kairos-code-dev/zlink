package systems.zlink.e2e.discoveryregistryha.client;

import systems.zlink.e2e.discoveryregistryha.client.scenarios.BasicDiscoveryScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.ClientScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrA2ClusterBridgeScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrA3ClusterBridgeScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrA4ThirdRegistryScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrB1FailoverScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrB2FailoverScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrB3RecoveryScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrC1EmbeddedRegistryScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrC2EmbeddedRegistryScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrC3EmbeddedRegistryScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrD1DirectEndpointScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrD2DirectEndpointScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrD3DirectEndpointScenario;
import systems.zlink.e2e.discoveryregistryha.client.scenarios.DrD4DirectEndpointScenario;
import systems.zlink.e2e.discoveryregistryha.client.support.ClientContext;
import systems.zlink.e2e.discoveryregistryha.client.support.ClientOptions;
import systems.zlink.e2e.discoveryregistryha.client.support.DiscoveryApiResult;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        ClientOptions options = ClientOptions.fromEnv();
        ClientScenario scenario = scenario(options.scenario());
        DiscoveryApiResult result = scenario.run(new ClientContext(options));
        System.out.println("scenario " + options.scenario() + " passed providers=" + result.providers());
    }

    private static ClientScenario scenario(String name) {
        return switch (name) {
            case "DR-A1" -> new BasicDiscoveryScenario();
            case "DR-A2" -> new DrA2ClusterBridgeScenario();
            case "DR-A3" -> new DrA3ClusterBridgeScenario();
            case "DR-A4" -> new DrA4ThirdRegistryScenario();
            case "DR-B1" -> new DrB1FailoverScenario();
            case "DR-B2" -> new DrB2FailoverScenario();
            case "DR-B3" -> new DrB3RecoveryScenario();
            case "DR-C1" -> new DrC1EmbeddedRegistryScenario();
            case "DR-C2" -> new DrC2EmbeddedRegistryScenario();
            case "DR-C3" -> new DrC3EmbeddedRegistryScenario();
            case "DR-D1" -> new DrD1DirectEndpointScenario();
            case "DR-D2" -> new DrD2DirectEndpointScenario();
            case "DR-D3" -> new DrD3DirectEndpointScenario();
            case "DR-D4" -> new DrD4DirectEndpointScenario();
            default -> throw new IllegalArgumentException("unknown scenario " + name);
        };
    }
}
