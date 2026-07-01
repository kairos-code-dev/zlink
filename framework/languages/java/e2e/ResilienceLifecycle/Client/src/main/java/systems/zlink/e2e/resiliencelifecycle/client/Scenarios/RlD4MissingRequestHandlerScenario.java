package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlD4MissingRequestHandlerScenario {
    private RlD4MissingRequestHandlerScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        ConsumerScenarioClient.unsupported("RL-D4", "error reply wire header round-trip harness is not implemented");
    }
}
