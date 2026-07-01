package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlB5DrainInflightScenario {
    private RlB5DrainInflightScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-b5");
    }
}
