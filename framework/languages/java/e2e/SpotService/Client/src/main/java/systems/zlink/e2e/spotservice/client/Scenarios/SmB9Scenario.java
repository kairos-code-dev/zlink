package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmB9Scenario {
    private SmB9Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("actor-join-admission");
    }
}
