package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmB5Scenario {
    private SmB5Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("actor-missing");
    }
}
