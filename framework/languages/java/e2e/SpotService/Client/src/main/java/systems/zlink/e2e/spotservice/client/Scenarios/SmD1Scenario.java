package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD1Scenario {
    private SmD1Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("actor-session");
    }
}
