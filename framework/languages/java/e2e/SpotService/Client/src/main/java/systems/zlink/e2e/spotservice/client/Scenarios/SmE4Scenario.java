package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmE4Scenario {
    private SmE4Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("timer-overrun");
    }
}
