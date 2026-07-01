package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmF1Scenario {
    private SmF1Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("route-mesh");
    }
}
