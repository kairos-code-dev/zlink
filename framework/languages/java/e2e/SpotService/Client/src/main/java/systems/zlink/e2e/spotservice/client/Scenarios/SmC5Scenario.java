package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmC5Scenario {
    private SmC5Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("spot-mesh-cross-node");
    }
}
