package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmA5Scenario {
    private SmA5Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("stage-wrapper");
    }
}
