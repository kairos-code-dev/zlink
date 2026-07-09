package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmQ9Scenario {
    private SmQ9Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("multi-node");
    }
}
