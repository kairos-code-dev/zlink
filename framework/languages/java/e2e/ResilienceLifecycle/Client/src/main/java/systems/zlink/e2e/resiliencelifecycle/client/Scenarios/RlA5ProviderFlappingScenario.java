package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlA5ProviderFlappingScenario {
    private RlA5ProviderFlappingScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("flapping");
    }
}
