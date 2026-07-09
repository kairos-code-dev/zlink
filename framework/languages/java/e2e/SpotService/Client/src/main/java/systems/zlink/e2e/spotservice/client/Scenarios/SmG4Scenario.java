package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmG4Scenario {
    private SmG4Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("bound-push-load");
    }
}
