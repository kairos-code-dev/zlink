package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlC1ClientHostLifecycleScenario {
    private RlC1ClientHostLifecycleScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-c1");
    }
}
