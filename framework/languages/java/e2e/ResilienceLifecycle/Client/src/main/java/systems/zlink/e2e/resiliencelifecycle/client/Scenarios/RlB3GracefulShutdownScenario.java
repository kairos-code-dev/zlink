package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlB3GracefulShutdownScenario {
    private RlB3GracefulShutdownScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-b3");
    }
}
