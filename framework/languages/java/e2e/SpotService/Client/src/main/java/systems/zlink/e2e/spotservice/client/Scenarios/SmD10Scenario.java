package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD10Scenario {
    private SmD10Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("stream-backpressure");
    }
}
