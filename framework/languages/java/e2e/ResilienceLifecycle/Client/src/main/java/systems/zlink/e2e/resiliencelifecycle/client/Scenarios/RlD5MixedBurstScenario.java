package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlD5MixedBurstScenario {
    private RlD5MixedBurstScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-d5");
    }
}
