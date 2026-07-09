package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmB2Scenario {
    private SmB2Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("remote-actor-session");
    }
}
