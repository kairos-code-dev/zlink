package systems.zlink.e2e.spotservice.client.Scenarios;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;

public final class SmC4Scenario {
    private SmC4Scenario() {
    }

    public static void run(GatewayScenarioClient gateway) {
        GatewayScenarioClient.unsupported("SM-C4", "runner uses the publisher role after gateway modes finish");
    }
}
