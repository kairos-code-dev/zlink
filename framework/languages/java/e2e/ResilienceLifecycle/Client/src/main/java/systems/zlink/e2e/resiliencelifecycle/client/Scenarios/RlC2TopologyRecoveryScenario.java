package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlC2TopologyRecoveryScenario {
    private RlC2TopologyRecoveryScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-c2");
    }
}
