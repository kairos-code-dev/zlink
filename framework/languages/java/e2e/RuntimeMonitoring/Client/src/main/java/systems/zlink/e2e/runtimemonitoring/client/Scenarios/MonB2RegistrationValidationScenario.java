package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonB2RegistrationValidationScenario {
    private MonB2RegistrationValidationScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        expectRejected(context, "bad-interval", "location runtime interval must be positive");
        expectRejected(context, "missing-socket", "monitoring socket source is not configured");
        expectRejected(context, "missing-spot", "monitoring spot source is not configured");
        System.out.println("scenario MON-B2 passed");
    }

    private static void expectRejected(
        MonitoringScenarioContext context,
        String name,
        String expectedMessage) {
        var result = context.validation(name);
        MonitoringScenarioContext.ensure(result.rejected(),
            "MON-B2 validation " + name + " unexpectedly started");
        MonitoringScenarioContext.ensure(result.message().contains(expectedMessage),
            "MON-B2 validation " + name + " returned unexpected error: " + result.message());
    }
}
