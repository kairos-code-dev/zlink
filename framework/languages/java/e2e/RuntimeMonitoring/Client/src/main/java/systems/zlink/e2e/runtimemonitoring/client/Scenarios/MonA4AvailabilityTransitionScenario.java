package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonA4AvailabilityTransitionScenario {
    private MonA4AvailabilityTransitionScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-a4");
    }
}
