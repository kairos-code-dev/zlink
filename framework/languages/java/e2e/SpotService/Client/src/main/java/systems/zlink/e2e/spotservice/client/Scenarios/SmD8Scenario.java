package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmD8Scenario {
    private SmD8Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("stream-reconnect");
    }
}
