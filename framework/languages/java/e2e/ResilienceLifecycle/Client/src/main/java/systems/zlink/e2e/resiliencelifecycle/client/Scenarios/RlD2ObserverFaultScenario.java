package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlD2ObserverFaultScenario {
    private RlD2ObserverFaultScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-d2");
    }
}
