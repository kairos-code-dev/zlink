package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD3Scenario {
    private SmD3Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("actor-session");
    }
}
