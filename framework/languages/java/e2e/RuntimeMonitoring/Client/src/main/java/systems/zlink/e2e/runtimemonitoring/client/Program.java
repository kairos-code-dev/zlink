package systems.zlink.e2e.runtimemonitoring.client;

import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA1SocketEventsScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA2LocationEventsScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA3SpotEventsScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA4AvailabilityTransitionScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonA5FixedKindsScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonB1KindFilterScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonB2RegistrationValidationScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonC1DispatchFailureScenario;
import systems.zlink.e2e.runtimemonitoring.client.Scenarios.MonD1FailureRecoveryScenario;
import systems.zlink.e2e.runtimemonitoring.client.Support.TriggerScenarioClient;
import systems.zlink.e2e.runtimemonitoring.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        try (TriggerScenarioClient trigger = new TriggerScenarioClient(
                Env.get("ZLINK_JAVA_E2E_TRIGGER_HTTP"))) {
            String scenario = Env.get("ZLINK_JAVA_E2E_SCENARIO", "all");
            if (!"all".equals(scenario)) {
                runOne(scenario, trigger);
                System.out.println("monitoring e2e result=passed");
                return;
            }
            MonA1SocketEventsScenario.run(trigger);
            MonA2LocationEventsScenario.run(trigger);
            MonA3SpotEventsScenario.run(trigger);
            MonA4AvailabilityTransitionScenario.run(trigger);
            MonA5FixedKindsScenario.run(trigger);
            MonB1KindFilterScenario.run(trigger);
            MonB2RegistrationValidationScenario.run(trigger);
            MonC1DispatchFailureScenario.run(trigger);
            MonD1FailureRecoveryScenario.run(trigger);
            System.out.println("monitoring e2e result=passed");
        }
    }

    private static void runOne(String scenario, TriggerScenarioClient trigger) {
        switch (scenario) {
            case "MON-A1" -> MonA1SocketEventsScenario.run(trigger);
            case "MON-A2" -> MonA2LocationEventsScenario.run(trigger);
            case "MON-A3" -> MonA3SpotEventsScenario.run(trigger);
            case "MON-A4" -> MonA4AvailabilityTransitionScenario.run(trigger);
            case "MON-A5" -> MonA5FixedKindsScenario.run(trigger);
            case "MON-B1" -> MonB1KindFilterScenario.run(trigger);
            case "MON-B2" -> MonB2RegistrationValidationScenario.run(trigger);
            case "MON-C1" -> MonC1DispatchFailureScenario.run(trigger);
            case "MON-D1" -> MonD1FailureRecoveryScenario.run(trigger);
            default -> throw new IllegalArgumentException("Unknown RuntimeMonitoring scenario '" + scenario + "'.");
        }
    }
}
