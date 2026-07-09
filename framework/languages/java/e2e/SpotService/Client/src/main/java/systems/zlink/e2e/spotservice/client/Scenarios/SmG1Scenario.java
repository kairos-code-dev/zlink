package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmG1Scenario {
    private SmG1Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("play-crash-recovery");
    }
}
