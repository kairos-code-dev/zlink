package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmA8Scenario {
    private SmA8Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("worker");
    }
}
