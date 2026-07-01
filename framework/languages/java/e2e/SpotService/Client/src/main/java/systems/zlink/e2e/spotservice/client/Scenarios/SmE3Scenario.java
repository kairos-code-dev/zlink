package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmE3Scenario {
    private SmE3Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("idle-timer");
    }
}
