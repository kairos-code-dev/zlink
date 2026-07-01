package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlD2ObserverFaultScenario {
    private RlD2ObserverFaultScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        ConsumerScenarioClient.unsupported("RL-D2", "observer failure runtime error sink assertion is not implemented");
    }
}
