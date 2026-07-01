package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlC2TopologyRecoveryScenario {
    private RlC2TopologyRecoveryScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        ConsumerScenarioClient.unsupported("RL-C2", "deterministic TTL stale-entry cleanup harness is not implemented");
    }
}
