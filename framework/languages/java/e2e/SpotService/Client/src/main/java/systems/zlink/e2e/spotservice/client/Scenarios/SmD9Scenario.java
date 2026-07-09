package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD9Scenario {
    private SmD9Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("actor-session");
    }
}
