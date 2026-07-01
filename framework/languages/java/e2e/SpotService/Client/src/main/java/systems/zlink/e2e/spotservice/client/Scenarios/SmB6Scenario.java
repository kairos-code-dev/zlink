package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmB6Scenario {
    private SmB6Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        gateway.runMode("actor-leave-disconnect");
    }
}
