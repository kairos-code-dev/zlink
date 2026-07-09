package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmF5Scenario {
    private SmF5Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("route-lifecycle");
    }
}
