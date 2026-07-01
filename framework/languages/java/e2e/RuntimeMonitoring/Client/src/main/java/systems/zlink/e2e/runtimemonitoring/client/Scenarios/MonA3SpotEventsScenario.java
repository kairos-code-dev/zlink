package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonA3SpotEventsScenario {
    private MonA3SpotEventsScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-a3");
    }
}
