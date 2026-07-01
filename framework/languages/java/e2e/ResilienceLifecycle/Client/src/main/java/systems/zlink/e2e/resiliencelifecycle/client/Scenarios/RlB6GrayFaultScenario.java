package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlB6GrayFaultScenario {
    private RlB6GrayFaultScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-b6");
    }
}
