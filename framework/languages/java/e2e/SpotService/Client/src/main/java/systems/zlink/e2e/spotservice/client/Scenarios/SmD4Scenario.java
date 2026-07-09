package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD4Scenario {
    private SmD4Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("multi-actor-bind");
    }
}
