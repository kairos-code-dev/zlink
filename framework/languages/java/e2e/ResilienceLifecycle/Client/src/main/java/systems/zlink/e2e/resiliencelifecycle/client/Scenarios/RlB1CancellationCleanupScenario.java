package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlB1CancellationCleanupScenario {
    private RlB1CancellationCleanupScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-b1");
    }
}
