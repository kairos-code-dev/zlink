package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonB1KindFilterScenario {
    private MonB1KindFilterScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-b1");
    }
}
