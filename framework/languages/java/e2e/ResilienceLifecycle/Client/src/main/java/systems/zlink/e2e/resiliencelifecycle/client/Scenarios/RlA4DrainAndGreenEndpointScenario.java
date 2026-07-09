package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlA4DrainAndGreenEndpointScenario {
    private RlA4DrainAndGreenEndpointScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rolling-green");
    }
}
