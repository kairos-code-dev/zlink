package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlC4RegistryOutageScenario {
    private RlC4RegistryOutageScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-c4");
    }
}
