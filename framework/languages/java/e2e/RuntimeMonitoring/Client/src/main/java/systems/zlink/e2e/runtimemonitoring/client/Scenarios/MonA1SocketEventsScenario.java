package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonA1SocketEventsScenario {
    private MonA1SocketEventsScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-a1");
    }
}
