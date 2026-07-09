package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD13Scenario {
    private SmD13Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("stream-heartbeat");
    }
}
