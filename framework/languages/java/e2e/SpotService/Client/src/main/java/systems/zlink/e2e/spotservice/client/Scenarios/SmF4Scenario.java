package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmF4Scenario {
    private SmF4Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("route-mesh");
    }
}
