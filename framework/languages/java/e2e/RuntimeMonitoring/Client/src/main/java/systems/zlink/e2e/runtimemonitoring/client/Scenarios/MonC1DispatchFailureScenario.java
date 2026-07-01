package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonC1DispatchFailureScenario {
    private MonC1DispatchFailureScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-c1");
    }
}
