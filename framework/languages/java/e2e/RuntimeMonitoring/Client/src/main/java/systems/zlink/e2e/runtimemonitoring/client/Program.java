package systems.zlink.e2e.runtimemonitoring.client;

import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA1SocketEventsScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA2RegistryEventsScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA3SpotEventsScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA5FixedKindsScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonB1KindFilterScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonB2RegistrationValidationScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonC1DispatchFailureScenario;
import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;
import systems.zlink.e2e.runtimemonitoring.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        TriggerScenarioClient trigger = new TriggerScenarioClient(Env.get("ZLINK_JAVA_E2E_TRIGGER_HTTP"));
        MonA1SocketEventsScenario.run(trigger);
        MonA2RegistryEventsScenario.run(trigger);
        MonA3SpotEventsScenario.run(trigger);
        MonA5FixedKindsScenario.run(trigger);
        MonB1KindFilterScenario.run(trigger);
        MonB2RegistrationValidationScenario.run(trigger);
        MonC1DispatchFailureScenario.run(trigger);
        System.out.println("monitoring e2e result=passed");
    }
}
