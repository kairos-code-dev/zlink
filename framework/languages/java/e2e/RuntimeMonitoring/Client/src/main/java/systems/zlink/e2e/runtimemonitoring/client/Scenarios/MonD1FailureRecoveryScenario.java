package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonD1FailureRecoveryScenario {
    private MonD1FailureRecoveryScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-d1");
    }
}
