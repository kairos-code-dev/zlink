package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmA7Scenario {
    private SmA7Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("owner");
    }
}
