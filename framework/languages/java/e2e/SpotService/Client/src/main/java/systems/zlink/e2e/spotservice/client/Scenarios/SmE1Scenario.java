package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmE1Scenario {
    private SmE1Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("missing");
    }
}
