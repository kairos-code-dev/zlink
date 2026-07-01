package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlD1HighFanoutScenario {
    private RlD1HighFanoutScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("storm");
    }
}
