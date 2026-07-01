package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;

public final class MonB2RegistrationValidationScenario {
    private MonB2RegistrationValidationScenario() {
    }

    public static void run(TriggerScenarioClient trigger) {
        trigger.runScenario("mon-b2");
    }
}
