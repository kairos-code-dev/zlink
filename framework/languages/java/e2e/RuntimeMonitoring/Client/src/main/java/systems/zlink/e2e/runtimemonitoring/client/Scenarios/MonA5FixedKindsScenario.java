package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonA5FixedKindsScenario {
    private MonA5FixedKindsScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-a5");
    }
}
