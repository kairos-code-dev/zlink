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
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;
import systems.zlink.e2e.runtimemonitoring.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        Env.configure(args);
        try (MonitoringScenarioContext context = new MonitoringScenarioContext()) {
            String scenario = Env.get("scenario", "all");
            if (!"all".equals(scenario)) {
                runOne(scenario, context);
                System.out.println("monitoring e2e result=passed");
                return;
            }
            MonA1SocketEventsScenario.run(context);
            MonA2LocationEventsScenario.run(context);
            MonA3SpotEventsScenario.run(context);
            MonA4AvailabilityTransitionScenario.run(context);
            MonA5FixedKindsScenario.run(context);
            MonB1KindFilterScenario.run(context);
            MonB2RegistrationValidationScenario.run(context);
            MonC1DispatchFailureScenario.run(context);
            MonD1FailureRecoveryScenario.run(context);
            System.out.println("monitoring e2e result=passed");
        }
    }

    private static void runOne(String scenario, MonitoringScenarioContext context) {
        switch (scenario) {
            case "MON-A1" -> MonA1SocketEventsScenario.run(context);
            case "MON-A2" -> MonA2LocationEventsScenario.run(context);
            case "MON-A3" -> MonA3SpotEventsScenario.run(context);
            case "MON-A4" -> MonA4AvailabilityTransitionScenario.run(context);
            case "MON-A5" -> MonA5FixedKindsScenario.run(context);
            case "MON-B1" -> MonB1KindFilterScenario.run(context);
            case "MON-B2" -> MonB2RegistrationValidationScenario.run(context);
            case "MON-C1" -> MonC1DispatchFailureScenario.run(context);
            case "MON-D1" -> MonD1FailureRecoveryScenario.run(context);
            default -> throw new IllegalArgumentException("Unknown RuntimeMonitoring scenario '" + scenario + "'.");
        }
    }
}
