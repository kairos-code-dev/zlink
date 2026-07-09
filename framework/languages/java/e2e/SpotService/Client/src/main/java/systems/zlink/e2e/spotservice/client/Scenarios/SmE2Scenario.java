package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmE2Scenario {
    private SmE2Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("state1");
    }
}
