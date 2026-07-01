package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmC2Scenario {
    private SmC2Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("spot-outbound");
    }
}
