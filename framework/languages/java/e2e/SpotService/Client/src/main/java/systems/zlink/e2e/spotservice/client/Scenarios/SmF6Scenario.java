package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmF6Scenario {
    private SmF6Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("spot-only-mesh");
    }
}
