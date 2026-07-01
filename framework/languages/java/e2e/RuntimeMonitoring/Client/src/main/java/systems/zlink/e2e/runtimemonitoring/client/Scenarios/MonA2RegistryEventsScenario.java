package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonA2RegistryEventsScenario {
    private MonA2RegistryEventsScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-a2");
    }
}
