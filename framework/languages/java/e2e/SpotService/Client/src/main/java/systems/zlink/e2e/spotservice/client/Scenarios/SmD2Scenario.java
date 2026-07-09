package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD2Scenario {
    private SmD2Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("remote-actor-session");
    }
}
