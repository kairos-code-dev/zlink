package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlB4RuntimeDrainScenario {
    private RlB4RuntimeDrainScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-b4");
    }
}
