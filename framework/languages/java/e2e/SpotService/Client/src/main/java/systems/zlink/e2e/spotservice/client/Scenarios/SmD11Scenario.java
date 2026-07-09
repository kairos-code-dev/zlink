package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD11Scenario {
    private SmD11Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("mixed-stream-channel");
    }
}
