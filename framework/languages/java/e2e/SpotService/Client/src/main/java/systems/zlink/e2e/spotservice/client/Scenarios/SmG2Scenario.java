package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmG2Scenario {
    private SmG2Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("owner-remap");
    }
}
