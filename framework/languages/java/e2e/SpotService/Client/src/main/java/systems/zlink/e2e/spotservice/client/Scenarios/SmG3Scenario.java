package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmG3Scenario {
    private SmG3Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("join-leave-race");
    }
}
