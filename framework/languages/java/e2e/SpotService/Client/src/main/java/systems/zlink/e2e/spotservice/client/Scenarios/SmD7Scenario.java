package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD7Scenario {
    private SmD7Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        GatewayScenarioClient.unsupported("SM-D7", "stream auth and pre-auth dispatch failure is not implemented");
    }
}
