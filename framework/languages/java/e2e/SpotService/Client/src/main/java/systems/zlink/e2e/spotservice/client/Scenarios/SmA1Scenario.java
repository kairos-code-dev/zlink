package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmA1Scenario {
    private SmA1Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("state1");
    }
}
