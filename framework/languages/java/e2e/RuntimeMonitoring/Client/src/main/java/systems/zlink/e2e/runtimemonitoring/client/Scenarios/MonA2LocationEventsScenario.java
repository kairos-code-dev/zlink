package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.util.Set;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonA2LocationEventsScenario {
    private MonA2LocationEventsScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        String service = context.serviceEndpoint();
        String serviceB = context.serviceBEndpoint();
        MonitoringScenarioContext.ensure(!serviceB.isBlank(), "MON-A2 requires service-b HTTP endpoint");
        int topologyBefore = context.latestEvidenceCount(
            service, "location", "TOPOLOGY_CHANGED", "topology");
        int entriesBefore = context.evidenceEntryCount(service);
        context.shutdownServiceB("MON-A2 expected service-b to stop");
        context.waitForEvidenceCountAfter(
            service, "location", "TOPOLOGY_CHANGED", "topology", entriesBefore,
            count -> count < topologyBefore,
            "MON-A2 location topology did not decrease from " + topologyBefore);

        int restoreEntries = context.evidenceEntryCount(service);
        context.restartServiceB();
        try {
            context.waitForPort(serviceB, true, "MON-A2 expected service-b to restart");
            context.waitForEvidenceCountAfter(
                service, "location", "TOPOLOGY_CHANGED", "topology", restoreEntries,
                count -> count >= topologyBefore,
                "MON-A2 location topology did not return to " + topologyBefore);
        } catch (RuntimeException error) {
            context.stopRestartedServiceB();
            throw error;
        }
        context.waitForEvent(service, "location", Set.of(
            "STATUS_CHANGED", "TOPOLOGY_CHANGED", "SERVICE_SUMMARY_CHANGED"));
        System.out.println("scenario MON-A2 passed");
    }
}
