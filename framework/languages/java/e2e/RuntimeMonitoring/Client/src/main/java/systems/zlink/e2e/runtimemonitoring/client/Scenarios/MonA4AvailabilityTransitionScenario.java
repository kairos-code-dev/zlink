package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.util.Set;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonA4AvailabilityTransitionScenario {
    private MonA4AvailabilityTransitionScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        String serviceA = context.serviceEndpoint();
        String serviceB = context.serviceBEndpoint();
        if (!serviceB.isBlank()) {
            context.post(serviceB, "/admin/drain");
        }
        context.post(serviceA, "/admin/restore");
        var before = context.requestFromProvider("mon-a4-before-drain", "svc-a");
        MonitoringScenarioContext.ensure(before.providerRid().equals("svc-a"),
            "MON-A4 direct trigger did not hit svc-a");
        context.post(serviceA, "/admin/drain");
        context.waitForTriggerEvent("socket", Set.of("PEER_ADMISSION_CHANGED"));
        context.waitForEvent(serviceA, "admin", Set.of("drain"));
        context.waitForEvent(serviceA, "location", Set.of("TOPOLOGY_CHANGED"));
        context.post(serviceA, "/admin/restore");
        if (!serviceB.isBlank()) {
            context.post(serviceB, "/admin/restore");
        }
        System.out.println("scenario MON-A4 passed");
    }
}
