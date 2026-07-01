package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmB1Scenario {
    private SmB1Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("actor-session");
    }
}
