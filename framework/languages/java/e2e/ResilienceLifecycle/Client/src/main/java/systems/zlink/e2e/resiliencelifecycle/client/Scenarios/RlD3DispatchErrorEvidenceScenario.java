package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlD3DispatchErrorEvidenceScenario {
    private RlD3DispatchErrorEvidenceScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-d3");
    }
}
