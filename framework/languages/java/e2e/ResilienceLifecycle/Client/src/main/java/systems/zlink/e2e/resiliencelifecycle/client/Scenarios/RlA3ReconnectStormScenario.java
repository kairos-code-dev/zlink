package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlA3ReconnectStormScenario {
    private RlA3ReconnectStormScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("storm");
    }
}
